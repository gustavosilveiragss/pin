#pragma once

#include <Arduino.h>
#include <functional>
#include <Ssd1306Display.h>
#include <heatshrink_decoder.h>

namespace pin {

// Streams one heatshrink + RLE encoded blob straight to the OLED, frame by frame.
class Player {
public:
    using AbortFn = std::function<bool()>; // polled per frame, true stops playback

    explicit Player(mrm::Ssd1306Display& display)
        : display_(display) {}

    void play(const char* path, uint32_t offset, uint32_t length, const AbortFn& abort);

private:
    void putPixels(uint8_t bits, int32_t count, const AbortFn& abort);
    void decodeByte(uint8_t value, const AbortFn& abort);

    mrm::Ssd1306Display& display_;
    heatshrink_decoder hsd_;
    int16_t x_ = 0;
    int16_t y_ = 0;
    int32_t runLength_ = -1;
    int32_t dupByte_ = -1;
    uint32_t lastFrame_ = 0;
    bool aborted_ = false;
};

} // namespace pin
