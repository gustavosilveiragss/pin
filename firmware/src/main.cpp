#include <marmota.h>

#include "Bundle.h"
#include "Player.h"
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

constexpr uint8_t kClicksWifi = 2; // clicks that open the wifi portal
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

bool g_next = false;
bool g_enterWifi = false;
bool g_enterDiag = false;
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

bool shouldStop() {
    pollNav();
    return g_next || g_enterWifi || g_enterDiag;
}

void holdUntilGesture() {
    g_next = false; // do not carry a stale click into the wait, or it never sleeps
    button.reset();
    power::cpuClock(kIdleClockMhz);
    while (!g_next && !g_enterWifi && !g_enterDiag) {
        pollNav();
        led.heartbeat();
        power::lightSleep(kIdlePollMs, kButtonPin);
    }
}

void holdImage(uint8_t seconds) {
    power::cpuClock(kIdleClockMhz);
    const uint32_t ms = uint32_t(seconds) * kMsPerSec;
    const uint32_t start = millis();
    while (millis() - start < ms && !g_next && !g_enterWifi && !g_enterDiag) {
        pollNav();
        led.heartbeat();
        power::lightSleep(kIdlePollMs, kButtonPin);
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
        led.heartbeat();
        power::lightSleep(kIdlePollMs, kButtonPin);
    }
    g_enterDiag = false;
}

void advanceSequential() {
    g_current = (g_current + 1) % bundle.count();
}

void advanceByMode() {
    if (bundle.mode() == pin::PlayMode::Shuffle && bundle.count() > 1) {
        int next;
        do {
            next = random(bundle.count());
        } while (next == g_current);
        g_current = next;
    } else {
        advanceSequential(); // loop
    }
}

void enterWifi() {
    g_enterWifi = false;
    led.on();
    const bool received = pin::runWifiMode(display, battery, button, bundle.ssid().c_str());
    led.off();
    if (received) {
        display.clear();
        display.line(24, "recebido!");
        display.show();
        delay(kReceivedMs);
    }
    bundle.load();
    g_current = 0;
}

} // namespace

void setup() {
    logBegin();
    led.begin();
    button.begin();
    randomSeed(micros());
    display.begin();
    battery.begin();

    if (!fsMount()) {
        display.clear();
        display.line(0, "LittleFS falhou");
        display.show();
        return;
    }
    bundle.load();
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
        display.clear();
        display.line(4, "sem conteudo");
        display.line(20, "clique 2x p/ wifi");
        display.show();
        holdUntilGesture();
        return;
    }

    const pin::Item& item = bundle.item(g_current);
    g_next = false;
    button.reset(); // a completed click from the previous item must not advance this one

    power::cpuClock(kDecodeClockMhz);
    player.play(bundle.path(), item.offset, item.length, shouldStop);

    if (g_enterWifi || g_enterDiag)
        return;
    if (g_next) {
        g_next = false;
        advanceSequential();
        return;
    }

    if (item.type == pin::ItemType::Image) {
        holdImage(item.holdSec);
        if (g_enterWifi || g_enterDiag)
            return;
        if (g_next) {
            g_next = false;
            advanceSequential();
            return;
        }
    }

    advanceByMode();
}
