#include <marmota.h>

#include <cstring>

#include "Bundle.h"
#include "Player.h"
#include "Proximity.h"
#include "Sinal.h"
#include "WifiMode.h"

using namespace mrm;
namespace sinal = pin::sinal;

namespace {

constexpr uint8_t kButtonPin = 9; // boot button, also the external case button
constexpr uint8_t kLedPin = 8;
constexpr uint8_t kBatteryPin = 3; // battery divider ADC node
constexpr uint32_t kDecodeClockMhz = 160;
constexpr uint32_t kIdleClockMhz = 80;
constexpr uint32_t kIdlePollMs = 50;
constexpr uint32_t kActivePollMs = 12; // dense poll while the radio blocks light sleep
constexpr uint32_t kMsPerSec = 1000;

constexpr uint32_t kUiFrameMs = 40;      // ~25 fps redraw, the single UI battery knob
constexpr uint32_t kDiagFrameMs = 250;   // the diag readout is near static, sample it lazily
constexpr uint32_t kBootFailMs = 500;    // crack redraw cadence on the dead filesystem screen
constexpr uint32_t kSplashMs = 1800;     // boot animation, one full screen cycle
constexpr uint32_t kReceivedMs = 1000;   // upload-received stamp, one full cycle (matches recebido)
constexpr uint32_t kEncounterMs = 2200;  // friend-arrival transition, one full cycle
constexpr uint32_t kInterruptMs = 700;   // overlay band lifetime, covers enter, hold and exit
constexpr size_t kBufferBytes = 1024;    // 128x64 / 8, the OLED pixel buffer

constexpr uint8_t kClicksWifi = 2; // wifi menu (code, status, upload)
constexpr uint8_t kClicksDiag = 5; // battery diagnostic

// 1.05 corrects the ~5% under-read from the 100k/100k divider source impedance
// (measured 3.99V raw at a full cell). Retune per unit from the diag screen.
constexpr float kBatteryCal = 1.05f;
constexpr float kDivider = 2.0f;        // 100k/100k
constexpr float kUsbThresholdV = 4.30f; // above a real cell means the reading is the usb rail
constexpr float kFullCellV = 4.2f;
constexpr uint8_t kDiagSamples = 64;

Ssd1306Display display;
Battery battery({.calibration = kBatteryCal});
Button button(kButtonPin);
Led led({.pin = kLedPin});
pin::Bundle bundle;
pin::Player player(display);
pin::Proximity proximity;

bool g_next = false;
bool g_enterWifi = false;
bool g_enterDiag = false;
bool g_friendShown = false; // true while the in-range handshake video is on screen
int g_current = 0;

void pollNav() {
    const uint8_t n = button.clicks();
    if (n == 0)
        return;
    if (n >= kClicksDiag)
        g_enterDiag = true;
    else if (n == kClicksWifi)
        g_enterWifi = true;
    else
        g_next = true;
}

// True when the on-screen state no longer matches presence, so playback must switch. Gated on
// active() so a stale g_friendShown can't force a permanent edge when the radio is off.
bool friendEdge() {
    return proximity.active() && proximity.present() != g_friendShown;
}

bool gesturePending() {
    return g_next || g_enterWifi || g_enterDiag;
}

bool shouldStop() {
    pollNav();
    proximity.tick();
    return gesturePending() || friendEdge();
}

// With the radio up, real light sleep would stop the WiFi modem and miss RX windows, so we
// busy-delay instead. Non-proximity pins keep the light-sleep idle unchanged.
void idleWait() {
    if (proximity.active())
        delay(kActivePollMs);
    else
        power::lightSleep(kIdlePollMs, kButtonPin);
}

void pump() {
    pollNav();
    proximity.tick();
    led.heartbeat();
}

// Static hold: no chrome to animate (held fila image, in-range image), keeps light sleep.
template <typename Keep>
void hold(Keep keep) {
    power::cpuClock(kIdleClockMhz);
    while (keep()) {
        pump();
        idleWait();
    }
}

// Animated SINAL screen that reacts to input: redraw at kUiFrameMs, pump nav every pass so
// beacon cadence and clicks are unaffected by the frame gate. idleWait() light-sleeps between
// frames when the radio is off (so the unbounded empty screen still deep-idles), busy-delays
// when it is up.
template <typename Draw, typename Keep>
void animate(Draw draw, Keep keep) {
    power::cpuClock(kIdleClockMhz);
    const uint32_t start = millis();
    uint32_t last = 0;
    while (keep()) {
        pump();
        const uint32_t now = millis();
        if (now - last >= kUiFrameMs) {
            draw(now - start);
            display.show();
            last = now;
        }
        idleWait();
    }
}

// Transient animation for a fixed span, no navigation (splash, received stamp).
template <typename Draw>
void playFor(Draw draw, uint32_t ms) {
    power::cpuClock(kIdleClockMhz);
    const uint32_t start = millis();
    uint32_t last = 0;
    while (millis() - start < ms) {
        led.heartbeat();
        const uint32_t now = millis();
        if (now - last >= kUiFrameMs) {
            draw(now - start);
            display.show();
            last = now;
        }
        idleWait();
    }
}

void holdImage(uint8_t seconds) {
    const uint32_t start = millis();
    const uint32_t ms = uint32_t(seconds) * kMsPerSec;
    hold([&] { return millis() - start < ms && !gesturePending() && !friendEdge(); });
}

// An in-range image is one frame, so loop() draws it once and then holds here rather than
// replaying it every pass (which would blank and redraw at full clock).
void holdWhilePresent() {
    hold([] { return proximity.present() && !gesturePending(); });
}

// A transient band over the frozen content frame on a click. The buffer is snapshotted once so
// each frame recomposites the band over the same still image, then playback advances.
void overlayFila() {
    battery.update();
    uint8_t bg[kBufferBytes];
    uint8_t* buf = display.raw().buffer;
    memcpy(bg, buf, kBufferBytes);
    const uint8_t tracks = min(3, bundle.count());
    const sinal::InterruptModel model{
        .kind = sinal::Overlay::Fila,
        .friendCode = nullptr,
        .battery = battery.percent(),
        .track = uint8_t(g_current % max<int>(1, tracks)),
        .trackCount = tracks,
    };
    power::cpuClock(kIdleClockMhz);
    const uint32_t start = millis();
    uint32_t last = 0;
    // pump() keeps the button state machine live so a fresh click during the band ends it early
    // instead of being swallowed (it would otherwise poll nothing for the whole span).
    while (millis() - start < kInterruptMs && !gesturePending()) {
        pump();
        const uint32_t now = millis();
        if (now - last >= kUiFrameMs) {
            memcpy(buf, bg, kBufferBytes);
            sinal::interrupcao(display.raw(), now - start, model);
            display.show();
            last = now;
        }
        idleWait();
    }
}

// Hidden battery diagnostic (5 clicks). Shows the raw divider reading so the calibration can be
// retuned: read "cru" on battery at a full charge, cal = 4.20 / cru. Read on battery, not usb
// (on usb the node sits on the ~5V rail). Click to exit.
void batteryDiag() {
    button.reset(); // drop the burst that opened the diag so a click can exit
    power::cpuClock(kIdleClockMhz);
    float lo = 9999.0f;
    float hi = 0.0f;
    uint32_t last = 0;
    while (button.clicks() == 0) {
        proximity.tick();
        led.heartbeat();
        const uint32_t now = millis();
        if (now - last >= kDiagFrameMs) {
            uint32_t sum = 0;
            for (uint8_t i = 0; i < kDiagSamples; ++i)
                sum += analogReadMilliVolts(kBatteryPin);
            const float node = float(sum) / kDiagSamples;
            lo = min(lo, node);
            hi = max(hi, node);
            const float cru = node * kDivider / kMsPerSec; // cell, uncalibrated
            const sinal::DiagModel model{
                .nodeMv = int(node),
                .minMv = int(lo),
                .maxMv = int(hi),
                .cru = cru,
                .cal = cru * kBatteryCal,
                .level = gfx::clamp01(cru / kFullCellV),
                .usb = cru > kUsbThresholdV,
            };
            sinal::diag(display.raw(), now, model);
            display.show();
            last = now;
        }
        idleWait();
    }
    g_enterDiag = false;
}

// The one handshake item is withheld from the fila only while the radio is up. Otherwise it
// just plays like any other item, so it is never silently unreachable.
bool filaSkip(int index) {
    return proximity.active() && index == bundle.inRangeIndex();
}

int firstFilaItem() {
    for (int i = 0; i < bundle.count(); ++i)
        if (!filaSkip(i))
            return i;
    return 0;
}

int filaPlayable() {
    int n = 0;
    for (int i = 0; i < bundle.count(); ++i)
        if (!filaSkip(i))
            ++n;
    return n;
}

void advanceSequential() {
    const int n = bundle.count();
    for (int k = 0; k < n; ++k) {
        g_current = (g_current + 1) % n;
        if (!filaSkip(g_current))
            return;
    }
}

void advanceByMode() {
    if (bundle.mode() == pin::PlayMode::Shuffle && filaPlayable() > 1) {
        int next;
        do {
            next = random(bundle.count());
        } while (next == g_current || filaSkip(next));
        g_current = next;
    } else {
        advanceSequential(); // loop
    }
}

// After playback, act on a pending event. Priority: menu, then friend arrival, then next.
// A single-click advance flashes the overlay band first. Returns true to restart loop().
bool interrupted() {
    if (g_enterWifi || g_enterDiag)
        return true;
    if (proximity.present())
        return true;
    if (g_next) {
        g_next = false; // consume the advancing click before the band, so it does not self-abort
        overlayFila();
        if (g_enterWifi || g_enterDiag)
            return true; // a menu gesture cut the band short: dispatch it, do not advance the fila
        advanceSequential();
        return true;
    }
    return false;
}

// Adopt /show.pin: load it, restart the radio for its target, rewind the fila.
void reloadBundle() {
    bundle.load();
    if (proximity.configure(bundle))
        display.reinit(); // the radio coming up disturbs the OLED charge pump
    g_current = firstFilaItem();
}

void enterWifi() {
    g_enterWifi = false;
    led.on();
    // Snapshot the link before the radio hands to the SoftAP, so the menu can report it.
    sinal::FriendState friendState;
    if (proximity.active())
        friendState = proximity.present() ? sinal::FriendState::Near : sinal::FriendState::Far;
    else if (bundle.proximityEnabled())
        friendState = sinal::FriendState::Error;
    else
        friendState = sinal::FriendState::None;
    const String friendCode = bundle.target();
    proximity.stop(); // the SoftAP needs the radio in AP mode
    const pin::PortalInfo info{
        .myCode = proximity.myCode(),
        .friend_ = friendState,
        .friendCode = friendCode.c_str(),
    };
    const bool received = pin::runWifiMode(display, battery, button, info);
    led.off();
    if (received)
        playFor([](uint32_t t) { sinal::recebido(display.raw(), t); }, kReceivedMs);
    button.reset();
    g_next = false;
    reloadBundle();
}

} // namespace

void setup() {
    logBegin();
    led.begin();
    button.begin();
    randomSeed(micros());
    display.begin();
    battery.begin();
    proximity.identify();

    playFor([](uint32_t t) { sinal::splash(display.raw(), t); }, kSplashMs);
    button.reset();

    if (!fsMount()) {
        // A broken filesystem is fatal and not "no content", so halt here with the crack tile
        // on screen instead of letting loop() invite a wifi upload that can never persist.
        // A dead device may sit here for hours, so light-sleep between the slow crack redraws
        // instead of busy-spinning. The crack only steps twice a second.
        power::cpuClock(kIdleClockMhz);
        uint32_t last = 0;
        const uint32_t start = millis();
        for (;;) {
            led.heartbeat();
            const uint32_t now = millis();
            if (now - last >= kBootFailMs) {
                sinal::bootFail(display.raw(), now - start);
                display.show();
                last = now;
            }
            power::lightSleep(kIdlePollMs, kButtonPin);
        }
    }
    reloadBundle();
}

void loop() {
    if (g_enterDiag) {
        batteryDiag(); // returns on a click
        return;
    }
    if (g_enterWifi) {
        enterWifi();
        return;
    }

    if (bundle.empty()) {
        g_friendShown = false;
        g_next = false;
        button.reset();
        animate([](uint32_t t) { sinal::semConteudo(display.raw(), t); },
                [] { return !gesturePending() && !friendEdge(); });
        return;
    }

    // A live handshake takes over with the in-range item. g_current stays put so the fila
    // resumes where it was on leave. The arrival plays the encontro transition once.
    if (proximity.present() && bundle.proximityEnabled()) {
        if (!g_friendShown) {
            const uint32_t start = millis();
            animate([](uint32_t t) { sinal::encontroEnter(display.raw(), t, {bundle.target().c_str()}); },
                    [&] { return millis() - start < kEncounterMs && proximity.present() && !gesturePending(); });
            // Bail only for a menu gesture or the friend leaving. A plain advance click falls
            // through and is consumed below (as HEAD did), so it can never livelock the branch.
            if (g_enterWifi || g_enterDiag || !proximity.present())
                return;
        }
        g_friendShown = true;
        const pin::Item& item = bundle.item(bundle.inRangeIndex());
        g_next = false;
        button.reset();
        power::cpuClock(kDecodeClockMhz);
        player.play(bundle.path(), item.offset, item.length, shouldStop);
        // A video replays on loop re-entry. An image is one frame, so hold it instead of redrawing.
        if (item.type == pin::ItemType::Image && !gesturePending() && proximity.present())
            holdWhilePresent();
        return;
    }
    g_friendShown = false;

    const pin::Item& item = bundle.item(g_current);
    g_next = false;
    button.reset(); // a completed click from the previous item must not advance this one

    power::cpuClock(kDecodeClockMhz);
    player.play(bundle.path(), item.offset, item.length, shouldStop);

    if (interrupted())
        return;
    if (item.type == pin::ItemType::Image) {
        holdImage(item.holdSec);
        if (interrupted())
            return;
    }
    advanceByMode();
}
