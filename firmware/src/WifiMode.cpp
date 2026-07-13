#include "WifiMode.h"
#include "Bundle.h"
#include "UploadPage.h"

#include <StatusScreen.h>
#include <WifiPortal.h>

namespace pin {

namespace {
constexpr uint32_t kRedrawMs = 500;
constexpr uint32_t kSettleMs = 50; // let the radio settle before the last redraw

} // namespace

bool runWifiMode(mrm::Ssd1306Display& display, mrm::Battery& battery, mrm::Button& button, const char* ssid, const char* myCode, const char* proxStatus) {
    const bool hasCode = myCode && myCode[0];
    const String code = hasCode ? String(myCode) : String();
    // Append the code to the AP name so a phone can read it from the wifi list, but only if
    // the "-XXXX" suffix still fits the 32-byte SSID cap; otherwise broadcast the plain name
    // (the code is still on the OLED "codigo" line and at GET /codigo).
    const bool codeInSsid = hasCode && String(ssid).length() + 5 <= 32;
    const String apSsid = codeInSsid ? String(ssid) + "-" + code : String(ssid);

    // The 2-click menu carries everything: this pin's own code (for the friend to type),
    // the proximity link state captured just before the radio handed over to the SoftAP,
    // and the upload hint.
    const String codeLine = hasCode ? String("codigo ") + code : String();
    const char* lines[4];
    uint8_t lineCount = 0;
    if (hasCode)
        lines[lineCount++] = codeLine.c_str();
    if (proxStatus && proxStatus[0])
        lines[lineCount++] = proxStatus;
    lines[lineCount++] = "envie 192.168.4.1";
    lines[lineCount++] = "clique p/ sair";

    mrm::WifiPortal portal({.ssid = apSsid.c_str(), .destPath = Bundle::kPath, .page = kUploadPage});
    portal.onValidate([](const char* tmp) { return Bundle::validate(tmp); });
    portal.onRoutes([code](WebServer& server) {
        server.on("/codigo", HTTP_GET, [code, &server] { server.send(200, "text/plain", code); });
    });

    display.reinit();
    display.clear();
    display.line(24, "iniciando wifi...");
    display.show();
    portal.begin();
    display.reinit(); // the radio powering up disturbs the OLED charge pump

    button.reset(); // drop the burst that opened wifi so a fresh click exits
    mrm::StatusScreen screen(display);
    uint8_t step = 0;
    uint32_t lastDraw = 0;
    bool first = true;
    while (!portal.done()) {
        portal.handle();
        if (first || millis() - lastDraw > kRedrawMs) {
            battery.update();
            const mrm::StatusScreen::Config status{
                .title = apSsid.c_str(),
                .lines = lines,
                .lineCount = lineCount,
                .battery = battery.percent(),
                .showBattery = true,
                .showWifi = true,
            };
            screen.draw(status, step++);
            lastDraw = millis();
            first = false;
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
