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
constexpr uint8_t kVersion = 0x02; // v2: bit0 do flags e o ack do handshake, nao o present local

// beacon de broadcast de 10 bytes. flags bit0 = sender ouve o alvo dele fresco (o ack do
// handshake), bit1 = sender tem alvo.
struct __attribute__((packed)) Beacon {
    uint8_t magic;
    uint8_t version;
    uint8_t flags;
    uint8_t sender[3]; // codigo de 20 bits
    uint8_t target[3]; // codigo de 20 bits, 0 quando nao configurado
    uint8_t seq;
};
static_assert(sizeof(Beacon) == 10, "beacon wire size");

constexpr uint32_t kBeaconPeriodMs = 100; // TX barato, o RX continuo ouve tudo

// Janela de frescor do handshake, 8 slots de beacon. Ouvir o amigo conta como fresco ate 7
// beacons seguidos se perderem (com 20% de perda por direcao, 8 misses seguidos e ~0.2^8 por
// slot, na pratica nunca), e o link cai em ate ~800ms quando o amigo some, os dois lados dentro
// de ~1 periodo de beacon um do outro.
constexpr uint32_t kFreshMs = 800;

uint32_t rd20(const uint8_t* p) {
    return (uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | uint32_t(p[2])) & 0xFFFFF;
}

void wr20(uint8_t* p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
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
    present_ = false;
    lastTxMs_ = 0;

    // Daqui o RF esta ligado (bomba de carga do OLED perturbada), entao todo caminho abaixo
    // retorna true. O radio fica em RX continuo: WIFI_PS_NONE desliga o power save do modem e
    // toda transmissao do amigo e ouvida, sem fase de janela de wake pra desalinhar os dois pins.
    // Custa mais bateria que um duty cycle, o preco do handshake simetrico deterministico.
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

    active_ = true;
    return true;
}

void Proximity::stop() {
    if (active_) {
        esp_now_unregister_recv_cb();
        if (s_active == this)
            s_active = nullptr;
        esp_now_deinit();
        WiFi.mode(WIFI_OFF);
    }
    active_ = false;
    present_ = false;
}

// True se o ultimo beacon mutuo do amigo chegou dentro da janela de frescor.
bool Proximity::hearsFresh(uint32_t now) const {
    return (now - lastRxMs_) < kFreshMs;
}

void Proximity::sendBeacon() {
    Beacon b;
    b.magic = kMagic;
    b.version = kVersion;
    // bit0 e o ack: este pin ouve o alvo dele fresco. O amigo so liga o present dele com esse
    // bit, entao o handshake exige o link nas duas direcoes.
    b.flags = uint8_t((hearsFresh(millis()) ? 0x01 : 0x00) | 0x02);
    wr20(b.sender, myUid_);
    wr20(b.target, myTarget_);
    b.seq = seq_++;
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(bcast, reinterpret_cast<const uint8_t*>(&b), sizeof(b));
}

void Proximity::tick() {
    if (!active_)
        return;
    const uint32_t now = millis();

    // present_ exige as duas direcoes: eu ouco o amigo fresco E o ultimo beacon dele confirma que
    // ele tambem me ouve fresco. Os dois lados sobem juntos em ~1 periodo de beacon e caem juntos
    // quando qualquer direcao seca (o ack do outro lado zera, ou o silencio passa da janela). Um
    // beacon perdido nao flapa, a janela cobre varios slots.
    present_ = hearsFresh(now) && peerAcks_ != 0;

    if (now - lastTxMs_ >= kBeaconPeriodMs) {
        sendBeacon();
        lastTxMs_ = now;
    }
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
    // O ack antes do timestamp: um tick que ve o lastRxMs_ fresco le um ack no minimo tao novo.
    peerAcks_ = uint32_t(b->flags & 0x01);
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
