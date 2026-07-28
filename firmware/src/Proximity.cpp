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
constexpr uint8_t kVersion = 0x01;

// beacon de broadcast de 10 bytes. flags bit0 = sender conectado, bit1 = sender tem alvo.
struct __attribute__((packed)) Beacon {
    uint8_t magic;
    uint8_t version;
    uint8_t flags;
    uint8_t sender[3]; // codigo de 20 bits
    uint8_t target[3]; // codigo de 20 bits, 0 quando nao configurado
    uint8_t seq;
};
static_assert(sizeof(Beacon) == 10, "beacon wire size");

// A WINDOW e o INTERVAL de wake sao ambos em milissegundos. O header do IDF 4.4 documenta errado
// a window como microssegundos (um typo conhecido, uma window em us e curta demais pra ouvir um
// beacon). A window fica acima do pior caso de espacamento do beacon pra toda window aberta pegar
// um beacon, dando deteccao deterministica sem zona morta de phase-lock. A duty de RX e
// window/interval, cerca de 5% em discovery e 2.5% conectado. Isso so limita o RF quando o
// sta_disconnected_pm esta ligado (ver configure()), entao o mA real ainda precisa de medida.
constexpr uint16_t kWakeWindowMs = 150;     // >= pior caso de espacamento do beacon (~133ms no video)
constexpr uint16_t kDiscIntervalMs = 3000;  // discovery: abre uma janela de escuta ~a cada 3 s
constexpr uint16_t kConnIntervalMs = 6000;  // conectado: mais preguicoso, so vigia a saida
constexpr uint32_t kBeaconPeriodMs = 100;   // TX livre (TX e barato, RX nao)
constexpr uint32_t kArriveFreshMs = 1600;   // um beacon mutuo tao recente => amigo esta aqui
constexpr uint32_t kLeaveMs = 30000;        // ~30 s de silencio => amigo saiu (volta pra fila)

uint32_t rd20(const uint8_t* p) {
    return (uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | uint32_t(p[2])) & 0xFFFFF;
}

void wr20(uint8_t* p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
}

} // namespace

volatile uint32_t Proximity::s_lastMatchMs = 0;
volatile uint32_t Proximity::s_matchCount = 0;
uint32_t Proximity::s_myUid = 0;
uint32_t Proximity::s_myTarget = 0;

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

    s_myUid = myUid_;
    s_myTarget = myTarget_;
    s_lastMatchMs = 0;
    s_matchCount = 0;

    // Daqui o RF esta ligado (bomba de carga do OLED perturbada), entao todo caminho abaixo
    // retorna true. O core padrao ja vem com CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE off,
    // entao WiFi.mode() deixaria o sta_disconnected_pm false e a window/interval ignoradas (radio
    // preso em RX continuo ~85mA). E um campo de init-config em runtime, entao re-inicia o wifi
    // com ele forcado ligado. O netif e o event loop padrao do WiFi.mode persistem nisso.
    WiFi.mode(WIFI_STA);
    esp_wifi_stop();
    esp_wifi_deinit();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.sta_disconnected_pm = true;
    if (esp_wifi_init(&cfg) != ESP_OK) {
        WiFi.mode(WIFI_OFF);
        return true;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
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
    esp_now_register_recv_cb(&Proximity::onRecv);

    setDuty(kWakeWindowMs, kDiscIntervalMs);
    present_ = false;
    lastTxMs_ = 0;
    active_ = true;
    return true;
}

void Proximity::stop() {
    if (active_) {
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        WiFi.mode(WIFI_OFF);
    }
    active_ = false;
    present_ = false;
}

void Proximity::setDuty(uint16_t windowMs, uint16_t intervalMs) {
    esp_now_set_wake_window(windowMs); // milissegundos
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
    esp_wifi_connectionless_module_set_wake_interval(intervalMs);
#else
    esp_wifi_set_connectionless_wake_interval(intervalMs);
#endif
}

void Proximity::sendBeacon() {
    Beacon b;
    b.magic = kMagic;
    b.version = kVersion;
    b.flags = uint8_t((present_ ? 0x01 : 0x00) | 0x02);
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
    const uint32_t last = s_lastMatchMs;
    const bool matched = s_matchCount > 0;

    // Um beacon mutuo e confiavel (match do codigo de 40 bits mais o CRC do ESP-NOW), entao a
    // chegada e imediata. O timeout de saida e a histerese que impede um beacon perdido de flapar.
    if (!present_) {
        if (matched && (now - last) < kArriveFreshMs) {
            present_ = true;
            setDuty(kWakeWindowMs, kConnIntervalMs);
        }
    } else {
        if (!matched || (now - last) > kLeaveMs) {
            present_ = false;
            setDuty(kWakeWindowMs, kDiscIntervalMs);
        }
    }

    if (now - lastTxMs_ >= kBeaconPeriodMs) {
        sendBeacon();
        lastTxMs_ = now;
    }
}

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void Proximity::onRecv(const struct esp_now_recv_info*, const uint8_t* data, int len) {
#else
void Proximity::onRecv(const uint8_t*, const uint8_t* data, int len) {
#endif
    if (len < int(sizeof(Beacon)))
        return;
    const Beacon* b = reinterpret_cast<const Beacon*>(data);
    if (b->magic != kMagic || b->version != kVersion)
        return;
    if (rd20(b->sender) == s_myTarget && rd20(b->target) == s_myUid) {
        s_lastMatchMs = millis();
        s_matchCount = s_matchCount + 1;
    }
}

} // namespace pin
