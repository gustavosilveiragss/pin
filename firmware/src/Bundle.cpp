#include "Bundle.h"

#include <LittleFS.h>
#include <cstring>

namespace pin {

namespace {

constexpr uint8_t kMagic[] = {'P', 'I', 'N', '1'};
constexpr uint8_t kVersion1 = 1;
constexpr uint8_t kVersion2 = 2; // adds the proximity target block + item flags
constexpr size_t kHeaderSize = 8; // magic(4) version(1) mode(1) count(1) flags(1)
constexpr size_t kSsidLenSize = 1;
constexpr size_t kTargetLenSize = 1;
constexpr size_t kIndexEntrySize = 8; // type(1) holdSec(1) itemFlags(1) reserved(1) length(4)
constexpr uint8_t kMaxSsidLen = 32;
constexpr uint8_t kModeMask = 0x03;
constexpr uint8_t kFlagInRangeOnly = 0x01;

constexpr size_t kHeaderVersion = 4;
constexpr size_t kHeaderMode = 5;
constexpr size_t kHeaderCount = 6;
constexpr size_t kEntryType = 0;
constexpr size_t kEntryHold = 1;
constexpr size_t kEntryFlags = 2; // repurposed from reserved; bit0 = in-range-only
constexpr size_t kEntryLength = 4;

uint32_t readU32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// Must match the alphabet in Proximity.cpp (32 symbols, no 0/1/I/O). A target that isn't a
// clean 4-char code can't be decoded to a UID, so proximity must read as OFF for it (and the
// starred item then plays normally in the fila instead of becoming unreachable).
constexpr char kCodeAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
bool codeValid(const String& s) {
    if (s.length() != 4)
        return false;
    for (size_t i = 0; i < 4; ++i)
        if (s[i] == '\0' || !strchr(kCodeAlphabet, s[i]))
            return false;
    return true;
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
    target_ = "";
    inRangeIndex_ = -1;
    playable_ = 0;

    File file = LittleFS.open(path, "r");
    if (!file || file.isDirectory()) {
        if (file)
            file.close();
        return false;
    }

    uint8_t header[kHeaderSize];
    const bool readHeader = file.size() >= kHeaderSize + kSsidLenSize && file.read(header, kHeaderSize) == kHeaderSize && memcmp(header, kMagic, sizeof(kMagic)) == 0;
    const uint8_t version = readHeader ? header[kHeaderVersion] : 0;
    const bool validHeader = readHeader && (version == kVersion1 || version == kVersion2);
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

    size_t targetBlock = 0;
    if (version == kVersion2) {
        uint8_t targetLen = 0;
        if (file.read(&targetLen, kTargetLenSize) != kTargetLenSize || targetLen > kMaxTargetLen) {
            file.close();
            return false;
        }
        char target[kMaxTargetLen + 1];
        const size_t targetRead = targetLen ? file.read(reinterpret_cast<uint8_t*>(target), targetLen) : 0;
        if (targetRead != targetLen) {
            file.close();
            return false;
        }
        target[targetLen] = 0;
        target_ = target;
        if (!codeValid(target_))
            target_ = ""; // undecodable target -> proximity off, starred item stays in the fila
        targetBlock = kTargetLenSize + targetLen;
    }

    const int count = header[kHeaderCount];
    if (count < 1 || count > kMaxItems) {
        file.close();
        return false;
    }
    mode_ = (header[kHeaderMode] & kModeMask) == uint8_t(PlayMode::Shuffle) ? PlayMode::Shuffle : PlayMode::Loop;

    uint32_t offset = kHeaderSize + kSsidLenSize + uint32_t(ssidLen) + uint32_t(targetBlock) + uint32_t(count) * kIndexEntrySize;
    int firstInRange = -1;
    int playable = 0;
    for (int i = 0; i < count; ++i) {
        uint8_t entry[kIndexEntrySize];
        if (file.read(entry, kIndexEntrySize) != kIndexEntrySize) {
            file.close();
            return false;
        }
        items_[i].type = ItemType(entry[kEntryType]);
        items_[i].holdSec = entry[kEntryHold];
        items_[i].flags = entry[kEntryFlags];
        items_[i].length = readU32(&entry[kEntryLength]);
        items_[i].offset = offset;
        offset += items_[i].length;
        if (items_[i].flags & kFlagInRangeOnly) {
            if (firstInRange < 0)
                firstInRange = i;
        } else {
            ++playable;
        }
    }

    const bool intact = offset == file.size();
    file.close();
    if (!intact)
        return false;
    count_ = count;
    inRangeIndex_ = firstInRange;
    playable_ = playable;
    return count_ > 0;
}

} // namespace pin
