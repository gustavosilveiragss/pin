#include "Proximity.h"
#include "Bundle.h"
#include "Code.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

namespace pin {

namespace {

constexpr uint8_t kChannel = 1; // every pin hard-codes the same channel (no scan/hop)

constexpr uint8_t kMagic = 0xB7;
constexpr uint8_t kVersion = 0x01;

// 10-byte broadcast beacon. flags bit0 = sender is connected, bit1 = sender has a target.
struct __attribute__((packed)) Beacon {
    uint8_t magic;
    uint8_t version;
    uint8_t flags;
    uint8_t sender[3]; // 20-bit code
    uint8_t target[3]; // 20-bit code, 0 when unconfigured
    uint8_t seq;
};
static_assert(sizeof(Beacon) == 10, "beacon wire size");

// Wake WINDOW and INTERVAL are both in milliseconds. The IDF 4.4 header wrongly documents the
// window as microseconds (a known typo, a us window is far too short to hear a beacon). The
// window sits above the worst-case beacon spacing so every opened window catches a beacon,
// giving deterministic detection with no phase-lock dead zone. RX duty is window/interval,
// about 5% discovery and 2.5% connected. This only gates the RF when sta_disconnected_pm
// engaged (see configure()), so the real mA still needs a bench check.
constexpr uint16_t kWakeWindowMs = 150;     // >= worst-case beacon spacing (~133ms in video)
constexpr uint16_t kDiscIntervalMs = 3000;  // discovery: open a listen window ~every 3 s
constexpr uint16_t kConnIntervalMs = 6000;  // connected: lazier, just watch for departure
constexpr uint32_t kBeaconPeriodMs = 100;   // free-running TX (TX is cheap, RX is not)
constexpr uint32_t kArriveFreshMs = 1600;   // a mutual beacon this recent => friend is here
constexpr uint32_t kLeaveMs = 30000;        // ~30 s of silence => friend left (back to fila)

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
    x *= 0x9E3779B1u; // Knuth multiplicative mix so adjacent factory MACs spread out
    uint32_t u = (x >> 12) & 0xFFFFF;
    if (u == 0)
        u = 1; // 0 is the "unconfigured" sentinel on the wire
    myUid_ = u;
    code::encode(u, myCode_);
    identified_ = true;
}

// Returns true if the RF was powered up, so the caller must reinit the OLED charge pump, even
// on a later init failure. Returns false when proximity never started (feature off or an
// unusable target) and the radio was left untouched.
bool Proximity::configure(const Bundle& bundle) {
    stop();
    identify();
    if (!bundle.proximityEnabled())
        return false;

    myTarget_ = code::decode(bundle.target());
    if (myTarget_ == 0 || myTarget_ == myUid_)
        return false; // undecodable, or our own code (never matches) => stay off, item in fila

    s_myUid = myUid_;
    s_myTarget = myTarget_;
    s_lastMatchMs = 0;
    s_matchCount = 0;

    // From here the RF is up (OLED charge pump disturbed) so every path below returns true.
    // The stock core ships CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE off, so WiFi.mode() would
    // leave sta_disconnected_pm false and the wake window/interval ignored (radio stuck in
    // continuous RX ~85mA). It is a runtime init-config field, so re-init the wifi with it
    // forced on. The netif and default event loop from WiFi.mode persist across this.
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
    if (esp_now_add_peer(&peer) != ESP_OK) { // without the peer, every send fails silently
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
    esp_now_set_wake_window(windowMs); // milliseconds
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

    // One mutual beacon is trusted (40-bit code match plus the ESP-NOW CRC), so arrival is
    // immediate. The leave timeout is the hysteresis that keeps a dropped beacon from flapping.
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
