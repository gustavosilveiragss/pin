#pragma once

#include <Arduino.h>

namespace pin {

class Bundle;

// Handshake de proximidade por ESP-NOW com TDMA de software. Cada pin abre janelas curtas de
// radio (beacons densos de TX + RX) e dorme entre elas, modem e CPU. O pin de uid menor e o
// master, referencia livre de tempo. O outro (chase) desliza a propria janela com um periodo
// menor ate cruzar a do master, le no beacon a fase ate a proxima janela dele e trava a agenda
// nela, reancorando a cada beacon ouvido pra absorver drift. Os dois lados rodam a mesma cadencia
// travada, sem troca de ritmo pra desalinhar a grade. O present_ so liga num handshake MUTUO
// confirmado: eu ouco um beacon cujo sender e o meu target e cujo target sou eu, e o bit de ack
// desse beacon diz que o amigo tambem esta me ouvindo fresco. Os beacons de TX saem do tick(), o
// RX cai num callback da task WiFi roteado pra instancia ativa.
class Proximity {
public:
    void identify();                      // deriva o myCode do id do chip (chama uma vez, cedo)
    bool configure(const Bundle& bundle); // (re)inicia o radio, true se o RF foi ligado
    void stop();                          // desliga o radio (libera pro portal de upload)
    void tick(bool keepOpen = false);     // pump: agenda da janela, handshake, TX do beacon.
                                          // keepOpen segura o radio aberto (playback ativo)

    bool active() const { return active_; }
    bool present() const { return present_; }      // handshake mutuo confirmado nos dois lados
    const char* myCode() const { return myCode_; } // 4 chars + NUL, valido depois de identify()

    // Agenda exposta pro idle do main dormir alinhado: inWindow() = radio aberto agora, o loop
    // fica acordado bombeando. msUntilNextWindow() = quanto dormir ate a proxima janela, 0 quando
    // nao ha agenda (radio off ou janela ja aberta), nunca 0 na borda pro caller nao dormir o
    // poll cheio em cima dela.
    bool inWindow() const { return active_ && open_; }
    uint32_t msUntilNextWindow() const;

    // Caminho de RX de um beacon cru. Em producao e chamado so pelo callback do ESP-NOW via
    // s_active. Publico como costura de teste: o simulador de host entrega beacons por aqui.
    void handleRx(const uint8_t* data, int len);

private:
    void consumeSync(uint32_t now);
    uint32_t schedulePeriod() const;
    void radioUp();
    void radioDown();
    void sendBeacon(uint32_t now);
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

    // Agenda da janela, toda tocada so pelo tick() (task main). openedAtMs_ e o inicio nominal da
    // janela corrente, nextOpenMs_ o da proxima. O master anda em grade propria, o chase reancora
    // nextOpenMs_ na fase dos beacons do master.
    bool master_ = false; // uid menor e a referencia, o maior desliza atras
    bool locked_ = false; // so no chase: agenda ancorada na fase do master
    bool open_ = false;
    uint32_t openedAtMs_ = 0;
    uint32_t nextOpenMs_ = 0;
    uint32_t periodMs_ = 0;
    uint32_t lastTxMs_ = 0;
    uint32_t syncSeenMs_ = 0; // ultimo lastRxMs_ ja consumido pelo consumeSync
    uint8_t seq_ = 0;

    // Escritos so pelo caminho de RX (task WiFi), lidos no tick() (task main). Um load/store
    // alinhado de 32 bits e atomico no RISC-V C3 e ha um unico escritor, entao nao precisa de
    // lock. A leitura do conjunto nao e atomica, mas o pior tear desloca uma janela ou atrasa uma
    // decisao em um beacon, e o proximo beacon corrige.
    volatile uint32_t lastRxMs_ = 0;  // millis() do ultimo beacon mutuo do amigo
    volatile uint32_t peerAcks_ = 0;  // bit de ack desse beacon: o amigo diz que me ouve fresco
    volatile uint32_t peerPhase_ = 0; // fase desse beacon: ms ate a proxima janela do sender

    // Instancia com o radio ligado, pro callback estatico do ESP-NOW rotear o beacon. Um pin so
    // tem uma instancia, o ponteiro existe pra logica ser instanciavel no simulador de host.
    static Proximity* s_active;
};

} // namespace pin
