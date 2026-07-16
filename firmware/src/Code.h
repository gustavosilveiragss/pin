#pragma once

#include <Arduino.h>

// Pairing code to 20-bit UID, shared by Proximity and Bundle. The web builder mirrors it.
namespace pin::code {

// 32 symbols, no 0/1/I/O so a spoken or handwritten code is unambiguous.
constexpr char kAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

inline int index(char c) {
    for (int i = 0; i < 32; ++i)
        if (kAlphabet[i] == c)
            return i;
    return -1;
}

// A usable code is exactly 4 alphabet symbols. Anything else is not a pairing target.
inline bool valid(const String& s) {
    if (s.length() != 4)
        return false;
    for (int i = 0; i < 4; ++i)
        if (index(s[i]) < 0)
            return false;
    return true;
}

// 4-char code -> 20-bit UID, or 0 (the "unconfigured" wire sentinel) if not a valid code.
inline uint32_t decode(const String& s) {
    if (!valid(s))
        return 0;
    uint32_t u = 0;
    for (int i = 0; i < 4; ++i)
        u = (u << 5) | uint32_t(index(s[i]));
    return u & 0xFFFFF;
}

// 20-bit UID -> 4-char code plus NUL.
inline void encode(uint32_t u, char out[5]) {
    for (int i = 0; i < 4; ++i)
        out[i] = kAlphabet[(u >> (5 * (3 - i))) & 0x1F];
    out[4] = 0;
}

} // namespace pin::code
