#pragma once

#include <SSD1306Wire.h>
#include <Gfx.h>
#include <Icons.h>

namespace pin {
namespace sinal {

enum class FriendState : uint8_t { Near, Far, Error, None };
enum class Overlay : uint8_t { Fila, Encontro };

struct PortalModel {
    const char* apSsid;
    const char* pin;
    uint8_t battery;
    FriendState friend_;
    const char* friendCode;
    const char* configHost;
    const char* uploadHost;
};

struct EncounterModel {
    const char* code;
};

struct InterruptModel {
    Overlay kind;
    const char* friendCode;
    uint8_t battery;
    uint8_t track;
    uint8_t trackCount;
};

struct DiagModel {
    int nodeMv;
    int minMv;
    int maxMv;
    float cru;
    float cal;
    float level; // 0..1 gauge
    bool usb;
};

// Time t is milliseconds since the screen was entered. Every screen clears except interrupcao,
// which composites over the frozen content frame. None call show(); the caller does.
void splash(SSD1306Wire& oled, uint32_t t);
void bootFail(SSD1306Wire& oled, uint32_t t);
void semConteudo(SSD1306Wire& oled, uint32_t t);
void interrupcao(SSD1306Wire& oled, uint32_t t, const InterruptModel& m);
void encontroEnter(SSD1306Wire& oled, uint32_t t, const EncounterModel& m);
void wifiStarting(SSD1306Wire& oled, uint32_t t);
void portal(SSD1306Wire& oled, uint32_t t, const PortalModel& m);
void recebido(SSD1306Wire& oled, uint32_t t);
void diag(SSD1306Wire& oled, uint32_t t, const DiagModel& m);

} // namespace sinal
} // namespace pin
