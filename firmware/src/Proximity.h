#pragma once

#include <Arduino.h>

namespace pin {

class Bundle;

// Handshake de proximidade por ESP-NOW. Cada pin configurado transmite um beacon pequeno com o
// proprio codigo e o codigo do amigo, e mostra seu video in-range so num match MUTUO (um beacon
// cujo sender e o meu target e cujo target sou eu). O radio sobe uma vez e e duty-cycle pelo
// power save connectionless do ESP-NOW, nunca ligando e desligando o WiFi, entao a bomba de carga
// do OLED so e perturbada no enable. Os beacons de TX saem do tick(), o RX cai num callback da
// task WiFi que guarda o timestamp do ultimo match mutuo.
class Proximity {
public:
    void identify();                      // deriva o myCode do id do chip (chama uma vez, cedo)
    bool configure(const Bundle& bundle); // (re)inicia o radio, true se o RF foi ligado
    void stop();                          // desliga o radio (libera pro portal de upload)
    void tick();                          // pump: TX do beacon + maquina de chegada/saida

    bool active() const { return active_; }
    bool present() const { return present_; }      // amigo mutuo no alcance (com debounce)
    const char* myCode() const { return myCode_; } // 4 chars + NUL, valido depois de identify()

private:
    void setDuty(uint16_t windowMs, uint16_t intervalMs);
    void sendBeacon();

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

    // Escrito so pelo callback de recv (task WiFi), lido no tick(). Um load/store alinhado de
    // 32 bits e atomico no RISC-V C3, e ha um unico escritor, entao nao precisa de lock.
    static volatile uint32_t s_lastMatchMs;
    static volatile uint32_t s_matchCount;
    static uint32_t s_myUid;
    static uint32_t s_myTarget;
};

} // namespace pin
