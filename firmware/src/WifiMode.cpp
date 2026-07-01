#include "WifiMode.h"
#include "Bundle.h"
#include "UploadPage.h"

#include <StatusScreen.h>
#include <WifiPortal.h>

namespace pin {

namespace {
constexpr uint32_t kRedrawMs = 500;
constexpr uint32_t kSettleMs = 50; // let the radio settle before the last redraw

const char* const kLines[] = {
    "pagina abre sozinha",
    "ou va em 192.168.4.1",
    "pin.marmota.dev.br",
    "clique para sair",
};
} // namespace

bool runWifiMode(mrm::Ssd1306Display& display, mrm::Battery& battery, mrm::Button& button, const char* ssid) {
    mrm::WifiPortal portal({.ssid = ssid, .destPath = Bundle::kPath, .page = kUploadPage});
    portal.onValidate([](const char* tmp) { return Bundle::validate(tmp); });

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
                .title = ssid,
                .lines = kLines,
                .lineCount = sizeof(kLines) / sizeof(kLines[0]),
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
