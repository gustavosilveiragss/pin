#include "WifiMode.h"
#include "Bundle.h"
#include "UploadPage.h"

#include <WifiPortal.h>

namespace pin {

namespace {
constexpr uint32_t kWifiStartMs = 1000; // animacao de wifi iniciando antes do radio bloquear
constexpr uint32_t kSettleMs = 50;      // deixa o radio assentar antes do ultimo redraw
constexpr const char* kApBase = "broche"; // o AP e sempre broche-<code>
constexpr const char* kConfigHost = "pin.marmota.dev.br";
constexpr const char* kUploadHost = "192.168.4.1";
} // namespace

bool runWifiMode(mrm::Ssd1306Display& display, mrm::Battery& battery, mrm::Button& button, const PortalInfo& info) {
    const bool hasCode = info.myCode && info.myCode[0];
    const String code = hasCode ? String(info.myCode) : String();
    // O codigo viaja no nome do AP pra um celular ler da lista de wifi (broche-<code>).
    // "broche-XXXX" tem 11 bytes, bem abaixo do cap de 32 bytes do SSID.
    const String apSsid = hasCode ? String(kApBase) + "-" + code : String(kApBase);

    mrm::WifiPortal portal({.ssid = apSsid.c_str(), .destPath = Bundle::kPath, .page = kUploadPage});
    portal.onValidate([](const char* tmp) { return Bundle::validate(tmp); });
    portal.onRoutes([code](WebServer& server) {
        server.on("/codigo", HTTP_GET, [code, &server] { server.send(200, "text/plain", code); });
    });

    display.reinit();
    for (uint32_t start = millis(), last = 0; millis() - start < kWifiStartMs;) {
        const uint32_t now = millis();
        if (now - last >= sinal::kUiFrameMs) {
            sinal::wifiStarting(display.raw(), now - start);
            display.show();
            last = now;
        }
        delay(10);
    }
    portal.begin();
    display.reinit(); // o radio ligando perturba a bomba de carga do OLED

    button.reset(); // descarta a rajada que abriu o wifi pra um clique novo sair
    const uint32_t start = millis();
    uint32_t last = 0;
    while (!portal.done()) {
        portal.handle();
        const uint32_t now = millis();
        if (now - last >= sinal::kUiFrameMs) {
            // Sem battery.update(): o AP puxa 100mA e a leitura sairia afundada. Mostra o
            // ultimo valor lido em idle, antes do radio subir.
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
