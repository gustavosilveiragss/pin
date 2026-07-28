#pragma once

#include <Battery.h>
#include <Button.h>
#include <Ssd1306Display.h>

#include "Sinal.h"

namespace pin {

// O que a tela do portal (duplo clique) mostra. `myCode` e o codigo de proximidade deste pin: ele
// e adicionado no nome do AP (broche-XXXX) e servido em GET /codigo pra um celular ler sem o OLED
// (importa no build sem botao). Passe "" pra omitir. friend_/friendCode reportam se um pin
// pareado estava no alcance, congelado antes do radio passar pro SoftAP.
struct PortalInfo {
    const char* myCode;
    sinal::FriendState friend_;
    const char* friendCode;
};

// Roda o portal de upload on-demand com a tela de portal do SINAL. Retorna true se um bundle
// valido foi recebido.
bool runWifiMode(mrm::Ssd1306Display& display, mrm::Battery& battery, mrm::Button& button, const PortalInfo& info);

} // namespace pin
