#pragma once

#include <Arduino.h>

// Codigo de pareamento pra UID de 20 bits, compartilhado por Proximity e Bundle. O web builder espelha.
namespace pin::code {

// 32 simbolos, sem 0/1/I/O pra um codigo falado ou escrito a mao nao ser ambiguo.
constexpr char kAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

inline int index(char c) {
    for (int i = 0; i < 32; ++i)
        if (kAlphabet[i] == c)
            return i;
    return -1;
}

// Um codigo usavel tem exatamente 4 simbolos do alfabeto. Qualquer outra coisa nao e alvo de pareamento.
inline bool valid(const String& s) {
    if (s.length() != 4)
        return false;
    for (int i = 0; i < 4; ++i)
        if (index(s[i]) < 0)
            return false;
    return true;
}

// codigo de 4 chars -> UID de 20 bits, ou 0 (o sentinela de "nao configurado" no fio) se invalido.
inline uint32_t decode(const String& s) {
    if (!valid(s))
        return 0;
    uint32_t u = 0;
    for (int i = 0; i < 4; ++i)
        u = (u << 5) | uint32_t(index(s[i]));
    return u & 0xFFFFF;
}

// UID de 20 bits -> codigo de 4 chars mais NUL.
inline void encode(uint32_t u, char out[5]) {
    for (int i = 0; i < 4; ++i)
        out[i] = kAlphabet[(u >> (5 * (3 - i))) & 0x1F];
    out[4] = 0;
}

} // namespace pin::code
