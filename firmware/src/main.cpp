#include <marmota.h>

#include "Bundle.h"
#include "Player.h"
#include "Proximity.h"
#include "WifiMode.h"

using namespace mrm;

namespace {

constexpr uint8_t kButtonPin = 9; // boot button, also the external case button
constexpr uint8_t kLedPin = 8;
constexpr uint8_t kBatteryPin = 3; // battery divider ADC node
constexpr uint32_t kDecodeClockMhz = 160;
constexpr uint32_t kIdleClockMhz = 80;
constexpr uint32_t kIdlePollMs = 50;
constexpr uint32_t kActivePollMs = 12; // dense poll while the radio blocks light sleep
constexpr uint32_t kMsPerSec = 1000;
constexpr uint32_t kReceivedMs = 700;

constexpr uint8_t kClicksWifi = 2; // wifi menu (code, status, upload)
constexpr uint8_t kClicksDiag = 5; // battery diagnostic

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

bool shouldStop() {
    pollNav();
    proximity.tick();
    return g_next || g_enterWifi || g_enterDiag || friendEdge();
}

// With the radio up, real light sleep would stop the WiFi modem and miss RX windows, so we
// busy-delay instead. Non-proximity pins keep the light-sleep idle unchanged.
void idleWait() {
    if (proximity.active())
        delay(kActivePollMs); // short so callers still catch fast clicks
    else
        power::lightSleep(kIdlePollMs, kButtonPin);
}

bool gesturePending() {
    return g_next || g_enterWifi || g_enterDiag;
}

// Low-power wait: clock down and pump input, radio and led until the predicate says stop.
template <typename KeepWaiting>
void holdWhile(KeepWaiting keepWaiting) {
    power::cpuClock(kIdleClockMhz);
    while (keepWaiting()) {
        pollNav();
        proximity.tick();
        led.heartbeat();
        idleWait();
    }
}

void holdUntilGesture() {
    g_next = false; // a carried-over click would keep it from ever sleeping
    button.reset();
    holdWhile([] { return !gesturePending() && !friendEdge(); });
}

void holdImage(uint8_t seconds) {
    const uint32_t start = millis();
    const uint32_t ms = uint32_t(seconds) * kMsPerSec;
    holdWhile([&] { return millis() - start < ms && !gesturePending() && !friendEdge(); });
}

// An in-range image is one frame, so loop() draws it once and then holds here rather than
// replaying it every pass (which would blank and redraw at full clock).
void holdWhilePresent() {
    holdWhile([] { return proximity.present() && !gesturePending(); });
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
// Returns true when loop() should restart from the top.
bool interrupted() {
    if (g_enterWifi || g_enterDiag)
        return true;
    if (proximity.present())
        return true;
    if (g_next) {
        g_next = false;
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
    String proxStatus;
    if (proximity.active())
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

    if (!fsMount()) {
        // A broken filesystem is fatal and not "no content", so halt here with the fault on
        // screen instead of letting loop() invite a wifi upload that can never persist.
        display.clear();
        display.line(8, "LittleFS falhou");
        display.line(28, "reinicie o pin");
        display.show();
        for (;;) {
            led.heartbeat();
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
        display.clear();
        display.line(4, "sem conteudo");
        display.line(20, "clique 2x p/ wifi");
        display.show();
        holdUntilGesture();
        return;
    }

    // A live handshake takes over with the in-range item. g_current stays put so the fila
    // resumes where it was on leave.
    if (proximity.present() && bundle.proximityEnabled()) {
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
