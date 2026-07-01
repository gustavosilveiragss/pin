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
    uint32_t offset = 0;
    uint32_t length = 0;
};

// A .pin bundle is one file the web tool builds and the pin just plays. Byte layout:
//
//   offset      size       field
//   0           4          magic 'P' 'I' 'N' '1'
//   4           1          version (1)
//   5           1          mode (0 loop, 2 shuffle)
//   6           1          count (items, 1..32)
//   7           1          flags (reserved, 0)
//   8           1          ssidLen (0..32, UTF-8 bytes of the WiFi name)
//   9           ssidLen    ssid (missing or >32 bytes falls back to kDefaultSsid)
//   9+ssidLen   count*8    index, one 8-byte entry per item
//   ...         ...        payload, the item blobs back to back
//
// Index entry (8 bytes): type(1) | holdSec(1) | reserved(2) | length(u32 little-endian)
//   type 0 = video (a stream of frames), 1 = image (one frame held holdSec seconds).
//
// Each blob is heatshrink(RLE(frames)). The Player decompresses and draws it. Item
// offsets are not stored: they are the running sum of the lengths after the index, and
// the bundle is accepted only if that sum lands exactly on the file size.
class Bundle {
public:
    static constexpr int kMaxItems = 32;
    static constexpr const char* kPath = "/show.pin";
    static constexpr const char* kDefaultSsid = "broche";

    bool load(const char* path = kPath);
    static bool validate(const char* path); // full structural check before promoting an upload

    bool empty() const { return count_ == 0; }
    int count() const { return count_; }
    PlayMode mode() const { return mode_; }
    const char* path() const { return kPath; }
    const String& ssid() const { return ssid_; }
    const Item& item(int index) const { return items_[index]; }

private:
    Item items_[kMaxItems];
    int count_ = 0;
    PlayMode mode_ = PlayMode::Loop;
    String ssid_ = kDefaultSsid;
};

} // namespace pin
