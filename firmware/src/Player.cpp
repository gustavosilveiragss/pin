#include "Player.h"

#include <LittleFS.h>

#if HEATSHRINK_DYNAMIC_ALLOC
#error "heatshrink must be static-alloc (HEATSHRINK_DYNAMIC_ALLOC=0)"
#endif

namespace pin {

namespace {

constexpr size_t kRleBuf = 4096;
constexpr size_t kReadBuf = 2048;
static_assert(kReadBuf <= HEATSHRINK_STATIC_INPUT_BUFFER_SIZE, "read chunk must fit the decoder input buffer");

constexpr uint8_t kEscBlack = 0x55; // marks a run of black
constexpr uint8_t kEscWhite = 0xAA; // marks a run of white
constexpr uint8_t kBlack = 0x00;
constexpr uint8_t kWhite = 0xFF;
constexpr uint8_t kRunSecondByte = 0x80; // high bit: another length byte follows
constexpr uint8_t kRunLowMask = 0x7F;
constexpr uint8_t kRunHighShift = 7;
constexpr uint8_t kMsb = 0x80; // pixels are packed most significant bit first
constexpr int kBitsPerByte = 8;
constexpr int kWidth = 128;
constexpr int kHeight = 64;
constexpr uint32_t kFrameMs = 33; // ~30 fps

} // namespace

void Player::play(const char* path, uint32_t offset, uint32_t length, const AbortFn& abort) {
    static uint8_t rleBuf[kRleBuf];
    static uint8_t compBuf[kReadBuf];

    File file = LittleFS.open(path, "r");
    if (!file || file.isDirectory()) {
        if (file)
            file.close();
        return;
    }
    if (!file.seek(offset)) {
        file.close();
        return;
    }

    display_.raw().clear();
    display_.raw().display();
    x_ = 0;
    y_ = 0;
    runLength_ = -1;
    dupByte_ = -1;
    aborted_ = false;
    lastFrame_ = millis();
    heatshrink_decoder_reset(&hsd_);

    uint32_t remaining = length;
    uint32_t sunkTotal = 0;
    size_t pending = 0;
    size_t head = 0;

    while ((remaining || pending) && !aborted_) {
        if (pending == 0) {
            const size_t toRead = min(kReadBuf, size_t(remaining));
            const size_t got = file.read(compBuf, toRead);
            if (got == 0)
                break;
            remaining -= got;
            pending = got;
            head = 0;
        }
        size_t sunk = 0;
        heatshrink_decoder_sink(&hsd_, &compBuf[head], pending, &sunk);
        pending -= sunk;
        head += sunk;
        sunkTotal += sunk;
        if (sunkTotal == length)
            heatshrink_decoder_finish(&hsd_);

        HSD_poll_res poll;
        do {
            size_t decoded = 0;
            poll = heatshrink_decoder_poll(&hsd_, rleBuf, kRleBuf, &decoded);
            if (poll < 0) {
                file.close();
                return;
            }
            for (size_t i = 0; i < decoded && !aborted_; ++i)
                decodeByte(rleBuf[i], abort);
        } while (poll == HSDR_POLL_MORE && !aborted_);
    }
    file.close();
}

// Fed one RLE byte at a time (the heatshrink output). Grammar:
//   - a plain byte is 8 packed pixels (MSB first), drawn as-is.
//   - 0x55 / 0xAA open a run of black / white, and the next byte(s) give the count:
//       0            emit the 0x55 / 0xAA byte literally once.
//       1..127       that many all-black (0x00) / all-white (0xFF) bytes.
//       high bit set low 7 bits here, then one more byte for bits 7..14 (up to 32767).
void Player::decodeByte(uint8_t value, const AbortFn& abort) {
    if (dupByte_ == -1) {
        if (value == kEscBlack || value == kEscWhite)
            dupByte_ = value;
        else
            putPixels(value, 1, abort);
        return;
    }
    if (runLength_ == -1) {
        if (value == 0) {
            putPixels(uint8_t(dupByte_), 1, abort);
            dupByte_ = -1;
        } else if ((value & kRunSecondByte) == 0) {
            putPixels(dupByte_ == kEscBlack ? kBlack : kWhite, value, abort);
            dupByte_ = -1;
        } else {
            runLength_ = value & kRunLowMask;
        }
        return;
    }
    runLength_ |= value << kRunHighShift;
    putPixels(dupByte_ == kEscBlack ? kBlack : kWhite, runLength_, abort);
    dupByte_ = -1;
    runLength_ = -1;
}

void Player::putPixels(uint8_t bits, int32_t count, const AbortFn& abort) {
    SSD1306Wire& oled = display_.raw();
    while (count-- > 0 && !aborted_) {
        uint8_t mask = kMsb;
        for (int i = 0; i < kBitsPerByte; ++i) {
            oled.setColor(bits & mask ? WHITE : BLACK);
            mask >>= 1;
            oled.setPixel(x_, y_);
            if (++x_ < kWidth)
                continue;
            x_ = 0;
            if (++y_ < kHeight)
                continue;
            y_ = 0;
            oled.display();
            if (abort()) {
                aborted_ = true;
                return;
            }
            const uint32_t elapsed = millis() - lastFrame_;
            if (elapsed < kFrameMs)
                delay(kFrameMs - elapsed);
            lastFrame_ = millis();
        }
    }
}

} // namespace pin
