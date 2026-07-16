#include "WifiMode.h"
#include "Bundle.h"
#include "UploadPage.h"

#include <WifiPortal.h>

namespace pin {

namespace {
constexpr uint32_t kUiFrameMs = 40;
constexpr uint32_t kWifiStartMs = 1000; // wifi-iniciando animation before the radio blocks
constexpr uint32_t kSettleMs = 50;      // let the radio settle before the last redraw
constexpr const char* kConfigHost = "pin.marmota.dev.br";
constexpr const char* kUploadHost = "192.168.4.1";
} // namespace

bool runWifiMode(mrm::Ssd1306Display& display, mrm::Battery& battery, mrm::Button& button, const PortalInfo& info) {
    const bool hasCode = info.myCode && info.myCode[0];
    const String code = hasCode ? String(info.myCode) : String();
    // Append the code to the AP name so a phone reads it from the wifi list, but only when the
    // "-XXXX" suffix still fits the 32-byte SSID cap. Otherwise broadcast the plain name.
    const bool codeInSsid = hasCode && String(info.ssid).length() + 5 <= 32;
    const String apSsid = codeInSsid ? String(info.ssid) + "-" + code : String(info.ssid);

    mrm::WifiPortal portal({.ssid = apSsid.c_str(), .destPath = Bundle::kPath, .page = kUploadPage});
    portal.onValidate([](const char* tmp) { return Bundle::validate(tmp); });
    portal.onRoutes([code](WebServer& server) {
        server.on("/codigo", HTTP_GET, [code, &server] { server.send(200, "text/plain", code); });
    });

    display.reinit();
    for (uint32_t start = millis(), last = 0; millis() - start < kWifiStartMs;) {
        const uint32_t now = millis();
        if (now - last >= kUiFrameMs) {
            sinal::wifiStarting(display.raw(), now - start);
            display.show();
            last = now;
        }
        delay(10);
    }
    portal.begin();
    display.reinit(); // the radio powering up disturbs the OLED charge pump

    button.reset(); // drop the burst that opened wifi so a fresh click exits
    const uint32_t start = millis();
    uint32_t last = 0;
    while (!portal.done()) {
        portal.handle();
        const uint32_t now = millis();
        if (now - last >= kUiFrameMs) {
            battery.update();
            const sinal::PortalModel model{
                .apSsid = apSsid.c_str(),
                .pin = hasCode ? code.c_str() : "----",
                .battery = battery.percent(),
                .friend_ = info.friend_,
                .friendCode = info.friendCode,
                .configHost = kConfigHost,
                .uploadHost = kUploadHost,
            };
            sinal::portal(display.raw(), now - start, model);
            display.show();
            last = now;
        }
        if (button.clicks() > 0)
            break;
    }

    const bool received = portal.done();
    portal.end();
    delay(kSettleMs);
    display.reinit();
    return received;
}

} // namespace pin
