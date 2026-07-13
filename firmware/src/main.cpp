#include <marmota.h>

#include "Bundle.h"
#include "Player.h"
#include "Proximity.h"
#include "WifiMode.h"

using namespace mrm;

namespace {

constexpr uint8_t kButtonPin = 9; // boot button, also the external case button
constexpr uint8_t kLedPin = 8;
constexpr uint8_t kBatteryPin = 3;        // battery divider ADC pin
constexpr uint32_t kDecodeClockMhz = 160; // full speed while decoding video
constexpr uint32_t kIdleClockMhz = 80;    // holds and idle polling
constexpr uint32_t kIdlePollMs = 50;      // light sleep window between polls
constexpr uint32_t kMsPerSec = 1000;
constexpr uint32_t kReceivedMs = 700; // "recebido!" confirmation on screen

constexpr uint8_t kClicksWifi = 2; // clicks that open the wifi menu (code + link status + upload)
constexpr uint8_t kClicksDiag = 5; // clicks that open the battery diagnostic

// 1.05 corrects the ~5% under-read from the 100k/100k divider source impedance
// (measured 3.99V raw at a full cell). Retune per unit from the diag screen.
constexpr float kBatteryCal = 1.05f;
constexpr float kDivider = 2.0f;        // 100k/100k
constexpr float kUsbThresholdV = 4.30f; // above a real cell means the reading is the usb rail
constexpr uint8_t kDiagSamples = 64;
constexpr uint32_t kDiagRedrawMs = 500;

Ssd1306Display display;
Battery battery({.calibration = kBatteryCal});
Button button(kButtonPin);
Led led({.pin = kLedPin});
pin::Bundle bundle;
pin::Player player(display);
pin::Proximity proximity;

bool g_fsOk = false; // LittleFS mounted -> the fila/upload paths are safe to use
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

// A proximity edge (friend arrived or left) must interrupt whatever is playing so loop()
// can switch between the fila and the in-range video. Gated on active(): when the radio is
// down present() is always false, so a stale g_friendShown can never force a permanent edge.
bool friendEdge() {
    return proximity.active() && proximity.present() != g_friendShown;
}

bool shouldStop() {
    pollNav();
    proximity.tick();
    return g_next || g_enterWifi || g_enterDiag || friendEdge();
}

// Idle wait. When the proximity radio is up, a real light sleep would power the WiFi modem
// down and stall the connectionless RX wake windows, so we stay in a plain delay (button is
// still polled every tick). Non-proximity pins keep the original light-sleep idle exactly.
void idleWait() {
    if (proximity.active())
        delay(kIdlePollMs);
    else
        power::lightSleep(kIdlePollMs, kButtonPin);
}

bool gesturePending() {
    return g_next || g_enterWifi || g_enterDiag;
}

void holdUntilGesture() {
    g_next = false; // do not carry a stale click into the wait, or it never sleeps
    button.reset();
    power::cpuClock(kIdleClockMhz);
    while (!gesturePending() && !friendEdge()) {
        pollNav();
        proximity.tick();
        led.heartbeat();
        idleWait();
    }
}

void holdImage(uint8_t seconds) {
    power::cpuClock(kIdleClockMhz);
    const uint32_t ms = uint32_t(seconds) * kMsPerSec;
    const uint32_t start = millis();
    while (millis() - start < ms && !gesturePending() && !friendEdge()) {
        pollNav();
        proximity.tick();
        led.heartbeat();
        idleWait();
    }
}

// Hold the already-drawn frame at low power for as long as the paired friend is present.
// Used when the in-range item is an IMAGE: draw it once, then idle here instead of
// re-play()ing every pass (which would blank+redraw ~30x/s at full clock).
void holdWhilePresent() {
    power::cpuClock(kIdleClockMhz);
    while (proximity.present() && !gesturePending()) {
        pollNav();
        proximity.tick();
        led.heartbeat();
        idleWait();
    }
}

// Hidden battery diagnostic (5 clicks). Shows the raw divider reading so the
// calibration can be retuned: read "cru" on battery at a full charge, cal = 4.20 / cru.
// Read on battery, not usb (on usb the node sits on the ~5V rail). Click to exit.
void batteryDiag() {
    button.reset(); // drop the burst that opened the diag so a click can exit
    power::cpuClock(kIdleClockMhz);
    uint32_t lastDraw = 0;
    float lo = 9999.0f;
    float hi = 0.0f;
    while (button.clicks() == 0) {
        if (millis() - lastDraw >= kDiagRedrawMs) {
            uint32_t sum = 0;
            for (uint8_t i = 0; i < kDiagSamples; ++i)
                sum += analogReadMilliVolts(kBatteryPin);
            const float node = float(sum) / kDiagSamples;
            lo = min(lo, node);
            hi = max(hi, node);
            const float cru = node * kDivider / kMsPerSec; // cell, uncalibrated
            const float cal = cru * kBatteryCal;           // what the gauge uses
            display.clear();
            display.line(0, "bateria diag");
            display.line(12, String("node ") + int(node) + "mV " + (cru > kUsbThresholdV ? "USB" : "BAT"));
            display.line(24, String("min ") + int(lo) + " max " + int(hi));
            display.line(36, String("cru ") + String(cru, 2) + "V cal " + String(cal, 2) + "V");
            display.show();
            lastDraw = millis();
        }
        proximity.tick();
        led.heartbeat();
        idleWait();
    }
    g_enterDiag = false;
}

// The in-range item is withheld from the normal fila only while the radio is actually up. If
// proximity isn't running (feature off, an unusable target, or a radio init failure) the
// flagged item just plays like any other item, so it is never silently unreachable.
bool filaSkip(int index) {
    return proximity.active() && bundle.item(index).inRangeOnly();
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
            return; // skip the dedicated in-range video, it only plays on a handshake
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

void enterWifi() {
    g_enterWifi = false;
    led.on();
    // Snapshot the link state before the radio hands over to the SoftAP, so the menu can
    // report whether the paired pin was in range.
    String proxStatus;
    if (proximity.active()) // snapshot the live link before the radio hands over to the AP
        proxStatus = String("amigo ") + bundle.target() + (proximity.present() ? " perto" : " longe");
    else if (bundle.proximityEnabled())
        proxStatus = "encontro: erro";
    else
        proxStatus = "sem encontro";
    proximity.stop(); // the SoftAP needs the radio in AP mode
    const bool received = pin::runWifiMode(display, battery, button, bundle.ssid().c_str(), proximity.myCode(), proxStatus.c_str());
    led.off();
    if (received) {
        display.clear();
        display.line(24, "recebido!");
        display.show();
        delay(kReceivedMs);
    }
    bundle.load();
    proximity.configure(bundle); // restart on channel 1 with the (possibly new) target
    if (proximity.active())
        display.reinit(); // radio back up after the AP teardown disturbs the charge pump
    g_current = firstFilaItem();
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

    if (!fsMount())
        return; // g_fsOk stays false -> loop() shows the mount error and holds
    g_fsOk = true;
    bundle.load();
    proximity.configure(bundle);
    if (proximity.active())
        display.reinit(); // bringing the radio up disturbs the OLED charge pump (once)
    g_current = firstFilaItem();
}

void loop() {
    if (!g_fsOk) {
        // A broken filesystem is not "no content": show the fault and hold, rather than
        // falling through to the empty-fila screen that invites a wifi upload that can't stick.
        display.clear();
        display.line(8, "LittleFS falhou");
        display.line(28, "reinicie o pin");
        display.show();
        for (;;) {
            led.heartbeat();
            power::lightSleep(kIdlePollMs, kButtonPin);
        }
    }
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
        display.clear();
        display.line(4, "sem conteudo");
        display.line(20, "clique 2x p/ wifi");
        display.show();
        holdUntilGesture();
        return;
    }

    // A live mutual handshake takes over the screen with the dedicated in-range item. The
    // fila position (g_current) is left untouched so it resumes on leave.
    if (proximity.present() && bundle.proximityEnabled()) {
        g_friendShown = true;
        const pin::Item& item = bundle.item(bundle.inRangeIndex());
        g_next = false;
        button.reset();
        power::cpuClock(kDecodeClockMhz);
        player.play(bundle.path(), item.offset, item.length, shouldStop);
        // A video just re-enters loop() and replays; an image is a single frame, so hold it
        // in low power while the friend stays instead of re-drawing it every pass.
        if (item.type == pin::ItemType::Image && !gesturePending() && proximity.present())
            holdWhilePresent();
        return; // re-enter: replays/holds while present, or falls back to the fila once gone
    }
    g_friendShown = false;

    const pin::Item& item = bundle.item(g_current);
    g_next = false;
    button.reset(); // a completed click from the previous item must not advance this one

    power::cpuClock(kDecodeClockMhz);
    player.play(bundle.path(), item.offset, item.length, shouldStop);

    if (g_enterWifi || g_enterDiag)
        return;
    if (proximity.present())
        return; // friend arrived: next loop shows the in-range video, keep g_current
    if (g_next) {
        g_next = false;
        advanceSequential();
        return;
    }

    if (item.type == pin::ItemType::Image) {
        holdImage(item.holdSec);
        if (g_enterWifi || g_enterDiag)
            return;
        if (proximity.present())
            return;
        if (g_next) {
            g_next = false;
            advanceSequential();
            return;
        }
    }

    advanceByMode();
}
