#pragma once

#include <Arduino.h>

namespace pin {

class Bundle;

// Handshake de proximidade por ESP-NOW. Cada pin configurado transmite um beacon pequeno com o
// proprio codigo e o codigo do amigo. O present_ so liga num handshake MUTUO confirmado: eu ouco
// um beacon cujo sender e o meu target e cujo target sou eu, e o bit de ack desse beacon diz que
// o amigo tambem esta me ouvindo fresco. Assim os dois lados ligam e desligam juntos, com skew de
// ~1 periodo de beacon. O radio fica em RX continuo (WIFI_PS_NONE): custa mais bateria que um
// duty cycle, em troca de recepcao deterministica sem fase de janela pra desalinhar. Os beacons
// de TX saem do tick(), o RX cai num callback da task WiFi roteado pra instancia ativa.
class Proximity {
public:
    void identify();                      // deriva o myCode do id do chip (chama uma vez, cedo)
    bool configure(const Bundle& bundle); // (re)inicia o radio, true se o RF foi ligado
    void stop();                          // desliga o radio (libera pro portal de upload)
    void tick();                          // pump: avaliacao do handshake + TX do beacon

    bool active() const { return active_; }
    bool present() const { return present_; }      // handshake mutuo confirmado nos dois lados
    const char* myCode() const { return myCode_; } // 4 chars + NUL, valido depois de identify()

    // Caminho de RX de um beacon cru. Em producao e chamado so pelo callback do ESP-NOW via
    // s_active. Publico como costura de teste: o simulador de host entrega beacons por aqui.
    void handleRx(const uint8_t* data, int len);

private:
    void sendBeacon();
    bool hearsFresh(uint32_t now) const;

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
    static void onRecv(const struct esp_now_recv_info* info, const uint8_t* data, int len);
#else
    static void onRecv(const uint8_t* mac, const uint8_t* data, int len);
#endif

    char myCode_[5] = {0};
    uint32_t myUid_ = 0;
    uint32_t myTarget_ = 0;
    bool identified_ = false;
    bool active_ = false;
    bool present_ = false;
    uint32_t lastTxMs_ = 0;
    uint8_t seq_ = 0;

    // Escritos so pelo caminho de RX (task WiFi), lidos no tick() (task main). Um load/store
    // alinhado de 32 bits e atomico no RISC-V C3 e ha um unico escritor, entao nao precisa de
    // lock. A leitura do par nao e atomica em conjunto, mas o pior tear atrasa uma decisao em um
    // beacon, dentro da tolerancia da janela de frescor.
    volatile uint32_t lastRxMs_ = 0; // millis() do ultimo beacon mutuo do amigo
    volatile uint32_t peerAcks_ = 0; // bit de ack desse beacon: o amigo diz que me ouve fresco

    // Instancia com o radio ligado, pro callback estatico do ESP-NOW rotear o beacon. Um pin so
    // tem uma instancia, o ponteiro existe pra logica ser instanciavel no simulador de host.
    static Proximity* s_active;
};

} // namespace pin
