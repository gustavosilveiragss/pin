#pragma once

#include <Arduino.h>

namespace pin {

enum class PlayMode : uint8_t { Loop = 0,
                                Shuffle = 2 }; // the only modes the web tool emits
enum class ItemType : uint8_t { Video = 0,
                                Image = 1 };

struct Item {
    ItemType type = ItemType::Video;
    uint8_t holdSec = 0;
    uint8_t flags = 0; // bit0 = in-range-only (the proximity/handshake video)
    uint32_t offset = 0;
    uint32_t length = 0;
    bool inRangeOnly() const { return flags & 0x01; }
};

// A .pin bundle is one file the web tool builds and the pin just plays. Byte layout:
//
//   offset      size       field
//   0           4          magic 'P' 'I' 'N' '1'
//   4           1          version (1, or 2 with the proximity block)
//   5           1          mode (0 loop, 2 shuffle)
//   6           1          count (items, 1..32)
//   7           1          flags (reserved, 0)
//   8           1          ssidLen (legacy WiFi-name length, ignored; the current tool writes 0)
//   9           ssidLen    ssid (legacy, ignored; the AP name is always broche-<code>)
//   (v2 only)   1          targetLen (0..8, the paired friend's short code)
//   (v2 only)   targetLen  target (ASCII base32 code the friend read off their pin)
//   ...         count*8    index, one 8-byte entry per item
//   ...         ...        payload, the item blobs back to back
//
// Index entry (8 bytes): type(1) | holdSec(1) | itemFlags(1) | reserved(1) | length(u32 LE)
//   type 0 = video (a stream of frames), 1 = image (one frame held holdSec seconds).
//   itemFlags bit0 = in-range-only: the dedicated handshake video, excluded from the
//   normal fila and shown only while a mutual proximity handshake is live (v1 = 0).
//
// Each blob is heatshrink(RLE(frames)). The Player decompresses and draws it. Item
// offsets are not stored: they are the running sum of the lengths after the index, and
// the bundle is accepted only if that sum lands exactly on the file size.
class Bundle {
public:
    static constexpr int kMaxItems = 32;
    static constexpr int kMaxTargetLen = 8;
    static constexpr const char* kPath = "/show.pin";

    bool load(const char* path = kPath);
    static bool validate(const char* path); // full structural check before promoting an upload

    bool empty() const { return count_ == 0; }
    int count() const { return count_; }
    PlayMode mode() const { return mode_; }
    const char* path() const { return kPath; }
    const Item& item(int index) const { return items_[index]; }

    // Proximity (in-range handshake video). Enabled only when the bundle carries a
    // valid friend code, one item is flagged in-range, and at least one item still
    // plays in the normal fila. When disabled every item plays normally (v1 behavior).
    // Whether the flagged item is actually withheld from the fila is decided at runtime
    // by the caller (it depends on the radio being up), not here.
    const String& target() const { return target_; }
    bool proximityEnabled() const { return target_.length() == 4 && inRangeIndex_ >= 0 && playable_ >= 1; }
    int inRangeIndex() const { return inRangeIndex_; }

private:
    Item items_[kMaxItems];
    int count_ = 0;
    PlayMode mode_ = PlayMode::Loop;
    String target_;
    int inRangeIndex_ = -1;
    int playable_ = 0;
};

} // namespace pin
