#pragma once

#include <Arduino.h>

namespace pin {

class Bundle;

// ESP-NOW proximity handshake. Each configured pin broadcasts a tiny beacon carrying its own
// code and the friend's code, and shows its in-range video only on a MUTUAL match (a beacon
// whose sender is my target and whose target is me). The radio comes up once and is
// duty-cycled by ESP-NOW connectionless power save, never by toggling WiFi on and off, so the
// OLED charge pump is disturbed only at enable. TX beacons are pumped from tick(), RX lands in
// a WiFi-task callback that records the timestamp of the last mutual match.
class Proximity {
public:
    void identify();                      // derive myCode from the chip id (call once, early)
    bool configure(const Bundle& bundle); // (re)start the radio, true if the RF was powered up
    void stop();                          // tear the radio down (frees it for the upload portal)
    void tick();                          // pump: beacon TX + arrive/leave state machine

    bool active() const { return active_; }
    bool present() const { return present_; }      // mutual friend in range (debounced)
    const char* myCode() const { return myCode_; } // 4 chars + NUL, valid after identify()

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

    // Written only by the recv callback (WiFi task), read in tick(). A 32-bit aligned
    // load/store is atomic on the RISC-V C3, and there is a single writer, so no lock.
    static volatile uint32_t s_lastMatchMs;
    static volatile uint32_t s_matchCount;
    static uint32_t s_myUid;
    static uint32_t s_myTarget;
};

} // namespace pin
