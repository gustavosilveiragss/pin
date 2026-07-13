#include "Proximity.h"
#include "Bundle.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

namespace pin {

namespace {

constexpr uint8_t kChannel = 1; // every pin hard-codes the same channel (no scan/hop)

constexpr uint8_t kMagic = 0xB7;
constexpr uint8_t kVersion = 0x01;
constexpr size_t kBeaconLen = 10;
constexpr size_t kOffFlags = 2;  // bit0 sender-connected hint, bit1 has-target
constexpr size_t kOffSender = 3; // 3 bytes, 20-bit code
constexpr size_t kOffTarget = 6; // 3 bytes, 20-bit code (0 = unconfigured)
constexpr size_t kOffSeq = 9;

// Duty cycle. The wake WINDOW is in MICROSECONDS, capped by the uint16 API at 65535us
// (~65ms) — it cannot be longer. The INTERVAL is in milliseconds. Because the window is
// short, the beacon TX is kept dense (100ms) so most opened windows still catch one; the
// leave timeout is generous so an occasional missed window does not flap the video.
// NOTE: this only gates the RF if sta_disconnected_pm is enabled (see configure()); the
// exact mA/latency must be bench-measured (see the plan's hardware gates).
constexpr uint16_t kWakeWindowUs = 65535;   // API max, ~65 ms of RX per interval
constexpr uint16_t kDiscIntervalMs = 3000;  // discovery: open a listen window ~every 3 s
constexpr uint16_t kConnIntervalMs = 6000;  // connected: lazier than discovery, but kept small
                                            // enough that ~8 windows fit inside kLeaveMs so a
                                            // run of missed windows can't spuriously drop the link
constexpr uint32_t kBeaconPeriodMs = 100;   // dense free-running TX (TX is cheap, RX is not)
constexpr uint32_t kArriveFreshMs = 1600;   // a mutual beacon this recent => friend is here
constexpr uint32_t kLeaveMs = 50000;        // ~8 connected windows of silence => friend left

// 32 symbols, no 0/1/I/O so a spoken/handwritten code is unambiguous.
constexpr char kAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

uint32_t rd20(const uint8_t* p) {
    return (uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | uint32_t(p[2])) & 0xFFFFF;
}

void wr20(uint8_t* p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
}

int alphaIndex(char c) {
    for (int i = 0; i < 32; ++i)
        if (kAlphabet[i] == c)
            return i;
    return -1;
}

// Decode a 4-char code the friend typed into the web tool back to its 20-bit value.
uint32_t decode4(const String& s) {
    if (s.length() < 4)
        return 0;
    uint32_t u = 0;
    for (int i = 0; i < 4; ++i) {
        const int v = alphaIndex(toupper(s[i]));
        if (v < 0)
            return 0;
        u = (u << 5) | uint32_t(v);
    }
    return u & 0xFFFFF;
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
    for (int i = 0; i < 4; ++i)
        myCode_[i] = kAlphabet[(u >> (5 * (3 - i))) & 0x1F];
    myCode_[4] = 0;
    identified_ = true;
}

void Proximity::configure(const Bundle& bundle) {
    stop();
    identify();
    if (!bundle.proximityEnabled())
        return;

    myTarget_ = decode4(bundle.target());
    if (myTarget_ == 0)
        return; // unparseable target => stay off

    s_myUid = myUid_;
    s_myTarget = myTarget_;
    s_lastMatchMs = 0;
    s_matchCount = 0;

    WiFi.mode(WIFI_STA);
    // The stock Arduino-ESP32 core has CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE off, so
    // WiFi.mode() inits the wifi layer with sta_disconnected_pm=false and the connectionless
    // wake window/interval below would be ignored (radio stuck in continuous RX ~85mA).
    // sta_disconnected_pm is a RUNTIME init-config field, so re-init the wifi layer with it
    // forced on. The netif + default event loop set up by WiFi.mode persist across this.
    esp_wifi_stop();
    esp_wifi_deinit();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.sta_disconnected_pm = true;
    if (esp_wifi_init(&cfg) != ESP_OK) {
        WiFi.mode(WIFI_OFF);
        return;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) {
        WiFi.mode(WIFI_OFF);
        return;
    }

    esp_now_peer_info_t peer = {};
    memset(peer.peer_addr, 0xFF, 6);
    peer.channel = kChannel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    esp_now_register_recv_cb(&Proximity::onRecv);

    setDuty(kWakeWindowUs, kDiscIntervalMs);
    present_ = false;
    lastTxMs_ = 0;
    active_ = true;
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

void Proximity::setDuty(uint16_t windowUs, uint16_t intervalMs) {
    esp_now_set_wake_window(windowUs); // microseconds (max 65535)
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
    esp_wifi_connectionless_module_set_wake_interval(intervalMs);
#else
    esp_wifi_set_connectionless_wake_interval(intervalMs);
#endif
}

void Proximity::sendBeacon() {
    uint8_t b[kBeaconLen];
    b[0] = kMagic;
    b[1] = kVersion;
    b[kOffFlags] = uint8_t((present_ ? 0x01 : 0x00) | 0x02);
    wr20(&b[kOffSender], myUid_);
    wr20(&b[kOffTarget], myTarget_);
    b[kOffSeq] = seq_++;
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(bcast, b, kBeaconLen);
}

void Proximity::tick() {
    if (!active_)
        return;
    const uint32_t now = millis();
    const uint32_t last = s_lastMatchMs;
    const bool matched = s_matchCount > 0;

    // A single mutual beacon is trusted: the 40-bit code match plus the ESP-NOW link CRC
    // make a false positive negligible, so arrival is immediate; the leave timeout is the
    // hysteresis that keeps a dropped beacon from flapping the in-range video.
    if (!present_) {
        if (matched && (now - last) < kArriveFreshMs) {
            present_ = true;
            setDuty(kWakeWindowUs, kConnIntervalMs);
        }
    } else {
        if (!matched || (now - last) > kLeaveMs) {
            present_ = false;
            setDuty(kWakeWindowUs, kDiscIntervalMs);
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
    if (len < int(kBeaconLen))
        return;
    if (data[0] != kMagic || data[1] != kVersion)
        return;
    const uint32_t sender = rd20(&data[kOffSender]);
    const uint32_t target = rd20(&data[kOffTarget]);
    if (sender == s_myTarget && target == s_myUid) {
        s_lastMatchMs = millis();
        s_matchCount = s_matchCount + 1;
    }
}

} // namespace pin
