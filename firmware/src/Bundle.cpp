#include "Bundle.h"

#include <LittleFS.h>
#include <cstring>

namespace pin {

namespace {

constexpr uint8_t kMagic[] = {'P', 'I', 'N', '1'};
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderSize = 8; // magic(4) version(1) mode(1) count(1) flags(1)
constexpr size_t kSsidLenSize = 1;
constexpr size_t kIndexEntrySize = 8; // type(1) holdSec(1) reserved(2) length(4)
constexpr uint8_t kMaxSsidLen = 32;
constexpr uint8_t kModeMask = 0x03;

constexpr size_t kHeaderVersion = 4;
constexpr size_t kHeaderMode = 5;
constexpr size_t kHeaderCount = 6;
constexpr size_t kEntryType = 0;
constexpr size_t kEntryHold = 1;
constexpr size_t kEntryLength = 4;

uint32_t readU32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

} // namespace

bool Bundle::validate(const char* path) {
    Bundle probe;
    return probe.load(path);
}

bool Bundle::load(const char* path) {
    count_ = 0;
    mode_ = PlayMode::Loop;
    ssid_ = kDefaultSsid;

    File file = LittleFS.open(path, "r");
    if (!file || file.isDirectory()) {
        if (file)
            file.close();
        return false;
    }

    uint8_t header[kHeaderSize];
    const bool validHeader = file.size() >= kHeaderSize + kSsidLenSize && file.read(header, kHeaderSize) == kHeaderSize && memcmp(header, kMagic, sizeof(kMagic)) == 0 && header[kHeaderVersion] == kVersion;
    if (!validHeader) {
        file.close();
        return false;
    }

    uint8_t ssidLen = 0;
    file.read(&ssidLen, kSsidLenSize);
    char ssid[256];
    const size_t ssidRead = ssidLen ? file.read(reinterpret_cast<uint8_t*>(ssid), ssidLen) : 0;
    if (ssidRead != ssidLen) {
        file.close();
        return false;
    }
    if (ssidLen >= 1 && ssidLen <= kMaxSsidLen) {
        ssid[ssidLen] = 0;
        ssid_ = ssid;
    }

    const int count = header[kHeaderCount];
    if (count < 1 || count > kMaxItems) {
        file.close();
        return false;
    }
    mode_ = (header[kHeaderMode] & kModeMask) == uint8_t(PlayMode::Shuffle) ? PlayMode::Shuffle : PlayMode::Loop;

    uint32_t offset = kHeaderSize + kSsidLenSize + uint32_t(ssidLen) + uint32_t(count) * kIndexEntrySize;
    for (int i = 0; i < count; ++i) {
        uint8_t entry[kIndexEntrySize];
        if (file.read(entry, kIndexEntrySize) != kIndexEntrySize) {
            file.close();
            return false;
        }
        items_[i].type = ItemType(entry[kEntryType]);
        items_[i].holdSec = entry[kEntryHold];
        items_[i].length = readU32(&entry[kEntryLength]);
        items_[i].offset = offset;
        offset += items_[i].length;
    }

    const bool intact = offset == file.size();
    file.close();
    if (intact)
        count_ = count;
    return count_ > 0;
}

} // namespace pin
