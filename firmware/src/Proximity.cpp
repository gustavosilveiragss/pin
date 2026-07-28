#include "Proximity.h"
#include "Bundle.h"
#include "Code.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

namespace pin {

namespace {

constexpr uint8_t kChannel = 1; // todo pin fixa o mesmo canal (sem scan/hop)

constexpr uint8_t kMagic = 0xB7;
constexpr uint8_t kVersion = 0x03; // v3: beacon carrega a fase ate a proxima janela do sender

// beacon de broadcast de 12 bytes. flags bit0 = sender ouve o alvo dele fresco (o ack do
// handshake), bit1 = sender tem alvo. phase = ms desta transmissao ate a proxima janela do
// sender, so o beacon do master ancora o receptor.
struct __attribute__((packed)) Beacon {
    uint8_t magic;
    uint8_t version;
    uint8_t flags;
    uint8_t sender[3]; // codigo de 20 bits
    uint8_t target[3]; // codigo de 20 bits, 0 quando nao configurado
    uint8_t seq;
    uint8_t phase[2];
};
static_assert(sizeof(Beacon) == 12, "beacon wire size");

// Janela e cadencias do TDMA. A descoberta desliza: o chase abre a cada kChasePeriodMs, o passo
// (kDiscPeriodMs - kChasePeriodMs = 80ms) varre o periodo do master inteiro sem furo porque fica
// abaixo do dobro da janela util (2 * (85 - 10 de subida do RF - 20 de espaco de TX - 10 de
// folga) = 90ms). Pior caso ate cruzar: ceil(420/80) = 6 ciclos de chase, ~2.0s, mais uma janela
// alinhada pro ack fechar, ~2.5s. Travado, o chase segue a mesma grade do master.
constexpr uint32_t kWinMs = 85;          // largura da janela de radio aberta
constexpr uint32_t kDiscPeriodMs = 420;  // cadencia do master, e do chase travado
constexpr uint32_t kChasePeriodMs = 340; // cadencia do chase destravado (o passo do varrido)
constexpr uint32_t kTxGapMs = 20;        // espaco entre beacons dentro da janela aberta

// Adianta a ancora do chase pra compensar a latencia entre a fase medida no send do master e o
// callback de RX aqui, e abrir um tico antes da janela dele.
constexpr uint32_t kChaseLeadMs = 5;

// Janela de frescor do handshake. Tolera duas janelas perdidas (a terceira chega em ate
// 3 * 420 - 85 = 1175ms), pra um corpo passando na frente nao piscar o link, e cai em ~1.3s
// quando o amigo some, os dois lados dentro de ~1 janela um do outro.
constexpr uint32_t kFreshMs = 1300;

// Silencio que derruba a trava do chase, ~6 janelas. Maior que kFreshMs de proposito: num blip
// curto o present ja caiu mas a agenda segue na grade do master e religa sem varrer.
constexpr uint32_t kLockLossMs = 2500;

uint32_t rd20(const uint8_t* p) {
    return (uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | uint32_t(p[2])) & 0xFFFFF;
}

void wr20(uint8_t* p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
}

uint32_t rd16(const uint8_t* p) {
    return uint32_t(p[0]) << 8 | uint32_t(p[1]);
}

void wr16(uint8_t* p, uint32_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

} // namespace

Proximity* Proximity::s_active = nullptr;

void Proximity::identify() {
    if (identified_)
        return;
    const uint64_t mac = ESP.getEfuseMac() & 0xFFFFFFFFFFFFULL;
    uint32_t x = uint32_t(mac ^ (mac >> 24) ^ (mac >> 45));
    x *= 0x9E3779B1u; // mistura multiplicativa de Knuth pra MACs de fabrica vizinhos se espalharem
    uint32_t u = (x >> 12) & 0xFFFFF;
    if (u == 0)
        u = 1; // 0 e o sentinela de "nao configurado" no fio
    myUid_ = u;
    code::encode(u, myCode_);
    identified_ = true;
}

// Retorna true se o RF foi ligado, entao o caller precisa reiniciar a bomba de carga do OLED,
// mesmo numa falha de init depois. Retorna false quando a proximidade nunca comecou (feature off
// ou um alvo inutil) e o radio ficou intocado.
bool Proximity::configure(const Bundle& bundle) {
    stop();
    identify();
    if (!bundle.proximityEnabled())
        return false;

    myTarget_ = code::decode(bundle.target());
    if (myTarget_ == 0 || myTarget_ == myUid_)
        return false; // indecodificavel, ou nosso proprio codigo (nunca casa) => fica off, item na fila

    // O estado do handshake nasce stale: lastRxMs_ recuado em kFreshMs deixa now - lastRxMs_ ja
    // fora da janela desde o primeiro tick (a aritmetica unsigned modular cobre o underflow perto
    // do boot).
    lastRxMs_ = millis() - kFreshMs;
    peerAcks_ = 0;
    peerPhase_ = 0;
    syncSeenMs_ = lastRxMs_;
    present_ = false;
    lastTxMs_ = 0;

    // Daqui o RF esta ligado (bomba de carga do OLED perturbada), entao todo caminho abaixo
    // retorna true. O driver sobe uma vez aqui, depois o tick() so alterna esp_wifi_start/stop
    // no ritmo das janelas.
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) {
        WiFi.mode(WIFI_OFF);
        return true;
    }

    esp_now_peer_info_t peer = {};
    memset(peer.peer_addr, 0xFF, 6);
    peer.channel = kChannel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) { // sem o peer, todo send falha silenciosamente
        esp_now_deinit();
        WiFi.mode(WIFI_OFF);
        return true;
    }
    s_active = this;
    esp_now_register_recv_cb(&Proximity::onRecv);

    // A comparacao e simetrica, os dois pins concordam nos papeis sem negociar.
    master_ = myUid_ < myTarget_;
    locked_ = false;

    // O RF ja esta de pe, a primeira janela comeca agora. O chase destravado desliza com o
    // periodo menor, ancora arbitraria (o varrido cobre qualquer defasagem).
    open_ = true;
    openedAtMs_ = millis();
    periodMs_ = master_ ? kDiscPeriodMs : kChasePeriodMs;
    nextOpenMs_ = openedAtMs_ + periodMs_;

    active_ = true;
    return true;
}

void Proximity::stop() {
    if (active_) {
        // Com a janela fechada o RF ja esta parado e o teardown do esp_now e so software.
        esp_now_unregister_recv_cb();
        if (s_active == this)
            s_active = nullptr;
        esp_now_deinit();
        WiFi.mode(WIFI_OFF);
    }
    active_ = false;
    present_ = false;
    open_ = false;
}

// True se o ultimo beacon mutuo do amigo chegou dentro da janela de frescor. Compara com sinal
// porque o callback da task WiFi pode gravar lastRxMs_ um tico a frente do now amostrado no tick.
bool Proximity::hearsFresh(uint32_t now) const {
    return int32_t(now - lastRxMs_) < int32_t(kFreshMs);
}

// Ritmo vigente na abertura da janela. Master e chase travado andam na cadencia de descoberta, o
// chase destravado desliza mais rapido pra cruzar a janela do master.
uint32_t Proximity::schedulePeriod() const {
    if (!master_ && !locked_)
        return kChasePeriodMs;
    return kDiscPeriodMs;
}

// Chase: adota a fase do ultimo beacon do master (escritos pelo caminho de RX). O par
// (lastRxMs_, peerPhase_) nao e atomico, um tear desloca uma janela e o proximo beacon corrige.
void Proximity::consumeSync(uint32_t now) {
    if (master_)
        return;
    const uint32_t rxAt = lastRxMs_;
    if (rxAt == syncSeenMs_)
        return;
    syncSeenMs_ = rxAt;
    locked_ = true;
    uint32_t next = rxAt + peerPhase_ - kChaseLeadMs;
    if (int32_t(next - now) < 0 || int32_t(next - now) > int32_t(2 * kDiscPeriodMs))
        next = now; // fase fora de faixa (beacon corrompido): abre agora, reancora no proximo
    nextOpenMs_ = next;
}

// Religa o RF pra janela. O start pode resetar canal e power save, refixa os dois.
void Proximity::radioUp() {
    esp_wifi_start();
    esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_ps(WIFI_PS_NONE);
}

void Proximity::radioDown() {
    esp_wifi_stop();
}

void Proximity::sendBeacon(uint32_t now) {
    Beacon b;
    b.magic = kMagic;
    b.version = kVersion;
    // bit0 e o ack: este pin ouve o alvo dele fresco. O amigo so liga o present dele com esse
    // bit, entao o handshake exige o link nas duas direcoes.
    b.flags = uint8_t((hearsFresh(now) ? 0x01 : 0x00) | 0x02);
    wr20(b.sender, myUid_);
    wr20(b.target, myTarget_);
    b.seq = seq_++;
    // Fase medida no send: com a janela aberta nextOpenMs_ ja aponta pra proxima, sempre futuro.
    const int32_t left = int32_t(nextOpenMs_ - now);
    wr16(b.phase, left > 0 ? uint32_t(left) : 0);
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(bcast, reinterpret_cast<const uint8_t*>(&b), sizeof(b));
}

void Proximity::tick(bool keepOpen) {
    if (!active_)
        return;
    const uint32_t now = millis();
    consumeSync(now);
    if (!master_ && locked_ && int32_t(now - lastRxMs_) > int32_t(kLockLossMs))
        locked_ = false; // perdeu o master, volta a deslizar

    // present_ exige as duas direcoes: eu ouco o amigo fresco E o ultimo beacon dele confirma que
    // ele tambem me ouve fresco. Os dois lados sobem juntos dentro de uma janela alinhada e caem
    // juntos quando qualquer direcao seca (o ack do outro lado zera, ou o silencio passa da
    // janela de frescor). Uma janela perdida nao flapa, o frescor cobre duas cadencias.
    present_ = hearsFresh(now) && peerAcks_ != 0;

    if (int32_t(now - nextOpenMs_) >= 0) {
        // Inicio nominal da grade: rearma a proxima pelo ritmo vigente e abre (ou segue aberta).
        periodMs_ = schedulePeriod();
        openedAtMs_ = nextOpenMs_;
        nextOpenMs_ += periodMs_;
        while (int32_t(now - nextOpenMs_) >= 0) {
            openedAtMs_ = nextOpenMs_; // um stall longo pega a janela corrente, nao a atrasada
            nextOpenMs_ += periodMs_;
        }
        if (!open_) {
            radioUp();
            open_ = true;
        }
    } else if (keepOpen && !open_) {
        // Playback segura o radio aberto fora da grade. A grade nao anda por isso, e o inicio
        // recuado faz a janela fechar assim que o keepOpen soltar.
        radioUp();
        open_ = true;
        openedAtMs_ = now - kWinMs;
    }

    if (open_ && !keepOpen && now - openedAtMs_ >= kWinMs) {
        radioDown();
        open_ = false;
    }

    if (open_ && now - lastTxMs_ >= kTxGapMs) {
        sendBeacon(now);
        lastTxMs_ = now;
    }
}

uint32_t Proximity::msUntilNextWindow() const {
    if (!active_ || open_)
        return 0;
    const int32_t left = int32_t(nextOpenMs_ - millis());
    return left > 0 ? uint32_t(left) : 1;
}

void Proximity::handleRx(const uint8_t* data, int len) {
    if (!active_)
        return; // um callback em voo durante o stop() nao escreve estado
    if (len < int(sizeof(Beacon)))
        return;
    const Beacon* b = reinterpret_cast<const Beacon*>(data);
    if (b->magic != kMagic || b->version != kVersion)
        return;
    if (rd20(b->sender) != myTarget_ || rd20(b->target) != myUid_)
        return;
    // O payload antes do timestamp: um tick que ve o lastRxMs_ fresco le campos no minimo tao novos.
    peerAcks_ = uint32_t(b->flags & 0x01);
    peerPhase_ = rd16(b->phase);
    lastRxMs_ = millis();
}

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void Proximity::onRecv(const struct esp_now_recv_info*, const uint8_t* data, int len) {
#else
void Proximity::onRecv(const uint8_t*, const uint8_t* data, int len) {
#endif
    Proximity* p = s_active; // copia local, o stop() pode zerar durante o voo
    if (p)
        p->handleRx(data, len);
}

} // namespace pin
