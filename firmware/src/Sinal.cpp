#include "Sinal.h"

using namespace mrm;

namespace pin {
namespace sinal {

namespace {

constexpr int16_t W = 128;
constexpr int16_t H = 64;

void text(SSD1306Wire& oled, int16_t x, int16_t y, const String& s, OLEDDISPLAY_TEXT_ALIGNMENT align = TEXT_ALIGN_LEFT) {
    oled.setTextAlignment(align);
    oled.drawString(x, y, s);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
}

void dashBox(SSD1306Wire& oled, int16_t x, int16_t y, int16_t w, int16_t h) {
    for (int16_t i = 0; i < w; i += 4) {
        oled.fillRect(x + i, y, 2, 1);
        oled.fillRect(x + i, y + h - 1, 2, 1);
    }
    for (int16_t i = 0; i < h; i += 4) {
        oled.fillRect(x, y + i, 1, 2);
        oled.fillRect(x + w - 1, y + i, 1, 2);
    }
}

// A scattered field of start points, hashed from the index so the coalesce is stable per frame.
void coalesceDots(SSD1306Wire& oled, int16_t cx, int16_t cy, float a) {
    for (uint8_t i = 0; i < 52; ++i) {
        const uint32_t h = (i + 1) * 2654435761u;
        const int16_t sx = (h >> 8) % W;
        const int16_t sy = (h >> 16) % H;
        oled.setPixel(sx + (cx - sx) * a, sy + (cy - sy) * a);
    }
}

// gfx::ping draws every positive radius; the screens want the preview window 2 < r < maxR so the
// opening rings do not flash a sub-pixel dot a frame before they read as a ring.
void pingSpec(SSD1306Wire& oled, int16_t cx, int16_t cy, const int16_t* radii, uint8_t count, int16_t maxR = 44) {
    int16_t keep[4];
    uint8_t n = 0;
    for (uint8_t i = 0; i < count && n < 4; ++i)
        if (radii[i] > 2 && radii[i] < maxR)
            keep[n++] = radii[i];
    gfx::ping(oled, cx, cy, keep, n);
}

} // namespace

void splash(SSD1306Wire& oled, uint32_t t) {
    oled.clear();
    oled.setColor(WHITE);
    const uint32_t dur = 1800;
    const float p = float(t % dur) / dur;
    const int16_t cx = 64, cy = 32;
    if (p < 0.42f) {
        coalesceDots(oled, cx, cy, gfx::easeOut(p / 0.42f));
        return;
    }
    const int16_t bob = lroundf(sinf(t / 260.0f));
    oled.fillCircle(cx, cy + bob, 14);
    oled.setColor(BLACK);
    for (int16_t i = 0; i < 16; ++i) {
        const int16_t w = max(1, int(lroundf(11 * (1 - abs(i - 8) / 8.0f))));
        oled.fillRect(cx - 5, cy - 8 + i + bob, w, 1);
    }
    oled.setColor(WHITE);
    if (p > 0.45f && p < 0.82f) {
        const float a = (p - 0.45f) / 0.37f;
        const int16_t radii[3] = {int16_t(a * 12), int16_t(a * 24 - 8), int16_t(a * 34 - 16)};
        pingSpec(oled, cx, cy + bob, radii, 3);
    }
}

void bootFail(SSD1306Wire& oled, uint32_t t) {
    oled.clear();
    const int16_t x = 48, y = 6, s = 32;
    oled.setColor(WHITE);
    oled.fillRect(x, y, s, s);
    oled.setColor(BLACK);
    const int16_t open = (t % 1000) / 500;
    const int16_t pts[4][2] = {{x, int16_t(y + s)}, {int16_t(x + s * 0.35f), int16_t(y + s * 0.55f)}, {int16_t(x + s * 0.55f), int16_t(y + s * 0.7f)}, {int16_t(x + s), y}};
    for (uint8_t i = 0; i < 3; ++i)
        gfx::thickLine(oled, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], 3 + open);
    oled.setColor(WHITE);
    text(oled, 64, 50, "reinicie", TEXT_ALIGN_CENTER);
}

void semConteudo(SSD1306Wire& oled, uint32_t t) {
    oled.clear();
    oled.setColor(WHITE);
    const uint32_t dur = 1800;
    const float p = float(t % dur) / dur;
    dashBox(oled, 40, 8, 48, 24);
    for (int16_t i = 0; i < 24; i += 4) {
        oled.fillRect(42, 10 + i, 2, 2);
        oled.fillRect(84, 10 + i, 2, 2);
    }
    oled.fillRect(62, 14, 4, 12);
    oled.fillRect(58, 18, 12, 4);
    oled.drawCircle(40, 48, 6);
    if (p < 0.25f || (p > 0.5f && p < 0.75f))
        oled.fillCircle(40, 48, 4);
    oled.drawCircle(54, 48, 6);
    if ((p > 0.25f && p < 0.5f) || p > 0.75f)
        oled.fillCircle(54, 48, 4);
    oled.drawLine(63, 48, 71, 48);
    oled.drawLine(71, 48, 68, 45);
    oled.drawLine(71, 48, 68, 51);
    gfx::wifiArcs(oled, 84, 54, int(p * 4) % 4 + 1, 4);
    text(oled, 47, 55, "2x", TEXT_ALIGN_CENTER);
}

void interrupcao(SSD1306Wire& oled, uint32_t t, const InterruptModel& m) {
    const uint32_t dur = 1030; // enter, hold and exit land inside the caller's overlay window
    const float p = float(t % dur) / dur;
    float vis = 0;
    if (p < 0.16f)
        vis = p / 0.16f;
    else if (p < 0.52f)
        vis = 1;
    else if (p < 0.68f)
        vis = 1 - (p - 0.52f) / 0.16f;
    if (vis <= 0)
        return;
    const int16_t rows = lroundf(12 * vis), top = 64 - rows;
    oled.setColor(BLACK);
    oled.fillRect(0, top, W, rows);
    oled.setColor(WHITE);
    oled.fillRect(0, top - 1, W, 1);
    if (vis <= 0.9f)
        return;
    if (m.kind == Overlay::Encontro) {
        gfx::drawIcon(oled, 2, 52, icons::Friends);
        text(oled, 24, 54, m.friendCode);
    } else {
        gfx::drawIcon(oled, 2, 52, icons::Video);
        for (uint8_t i = 0; i < m.trackCount; ++i) {
            const int16_t x = 56 + i * 10;
            if (i == m.track)
                oled.fillCircle(x, 58, 1);
            else
                oled.drawCircle(x, 58, 1);
        }
        gfx::drawIcon(oled, 110, 54, icons::Battery);
    }
}

void encontroEnter(SSD1306Wire& oled, uint32_t t, const EncounterModel& m) {
    oled.clear();
    oled.setColor(WHITE);
    const uint32_t dur = 2200;
    const float p = float(t % dur) / dur;
    if (p < 0.64f) {
        const float a = gfx::easeOut(gfx::clamp01(p / 0.46f));
        gfx::drawIcon(oled, lroundf(-24 + 64 * a), 24, icons::Person, 2);
        gfx::drawIcon(oled, lroundf(128 - 60 * a), 24, icons::Person, 2);
        const float s = gfx::clamp01((p - 0.4f) / 0.24f);
        if (s > 0) {
            const int16_t radii[2] = {int16_t(s * 12), int16_t(s * 22 - 6)};
            pingSpec(oled, 64, 30, radii, 2, 32);
        }
        if (p > 0.46f) {
            const bool on = (t / 120) % 2;
            oled.setColor(on ? WHITE : BLACK);
            oled.fillRect(44, 50, 40, 12);
            oled.setColor(on ? BLACK : WHITE);
            text(oled, 64, 51, m.code, TEXT_ALIGN_CENTER);
            oled.setColor(WHITE);
        }
        return;
    }
    gfx::drawIcon(oled, 44, 24, icons::Person, 2);
    gfx::drawIcon(oled, 64, 24, icons::Person, 2);
    const float a = (p - 0.64f) / 0.36f;
    gfx::flapCover(oled, W, ceil(16 * (1 - a)), 4);
}

void wifiStarting(SSD1306Wire& oled, uint32_t t) {
    oled.clear();
    oled.setColor(WHITE);
    const uint32_t dur = 1200;
    gfx::wifiArcs(oled, 64, 44, int((t % dur) / 300) % 4, 10);
    oled.fillRect(58, 46, 12, 4);
    for (int16_t i = 0; i < 3; ++i) {
        const int16_t yy = 42 - int16_t((t / 120 + i * 6) % 26);
        oled.setPixel(63, yy);
        oled.setPixel(64, yy);
    }
}

void portal(SSD1306Wire& oled, uint32_t t, const PortalModel& m) {
    oled.clear();
    oled.setColor(WHITE);
    oled.fillRect(0, 0, W, 12);
    oled.setColor(BLACK);
    gfx::drawIcon(oled, 2, 1, icons::Wifi);
    text(oled, 16, 1, m.apSsid);
    gfx::drawIcon(oled, 84, 2, icons::Battery); // left of x90 so a 3-digit "100%" clears the icon
    text(oled, 126, 1, String(m.battery) + "%", TEXT_ALIGN_RIGHT);

    oled.setColor(WHITE);
    oled.fillRect(10, 14, 108, 13);
    oled.setColor(BLACK);
    text(oled, 64, 16, m.pin, TEXT_ALIGN_CENTER);

    oled.setColor(WHITE);
    const bool far = m.friend_ == FriendState::Far || m.friend_ == FriendState::None;
    if (far)
        gfx::drawIconDither(oled, 3, 28, icons::Friends);
    else
        gfx::drawIcon(oled, 3, 28, icons::Friends);
    const char* label = m.friend_ == FriendState::Error ? "erro no link" : m.friend_ == FriendState::None ? "----" : m.friendCode;
    text(oled, 24, 29, label);
    gfx::drawIcon(oled, 5, 40, icons::Gear);
    text(oled, 24, 41, m.configHost);
    gfx::drawIcon(oled, 5, 52, icons::Upload);
    text(oled, 24, 53, m.uploadHost);
}

void recebido(SSD1306Wire& oled, uint32_t t) {
    oled.clear();
    oled.setColor(WHITE);
    const uint32_t dur = 1000;
    const float p = float(t % dur) / dur;
    gfx::drawIconReveal(oled, 46, 18, icons::Check, lroundf(gfx::easeOut(gfx::clamp01(p / 0.4f)) * 18), 2);
    if (p > 0.4f) {
        const float a = (p - 0.4f) / 0.45f;
        const int16_t radii[3] = {int16_t(16 + a * 10), int16_t(26 + a * 12 - 8), int16_t(a * 44 - 20)};
        pingSpec(oled, 64, 32, radii, 3);
    }
    if (p > 0.12f && p < 0.20f)
        gfx::invertFlash(oled);
}

void diag(SSD1306Wire& oled, uint32_t t, const DiagModel& m) {
    oled.clear();
    oled.setColor(WHITE);
    oled.drawRect(0, 0, W, H);
    oled.drawRect(2, 2, W - 4, H - 4);
    oled.drawRect(8, 14, 20, 38);
    oled.fillRect(15, 11, 6, 3);
    const int16_t seg = lroundf(m.level * 6);
    for (int16_t i = 0; i < seg; ++i)
        oled.fillRect(11, 48 - i * 6, 14, 4);
    text(oled, 40, 12, String(m.nodeMv) + "mV");
    // BAT/USB badge shares the mV row, right side: the 3-letter label needs ~21px, more than the
    // old 14px footer pill, and here it stops covering the cru/cal readout below.
    oled.fillRect(100, 11, 24, 11);
    oled.setColor(BLACK);
    text(oled, 112, 12, m.usb ? "USB" : "BAT", TEXT_ALIGN_CENTER);
    oled.setColor(WHITE);
    oled.fillRect(40, 30, 84, 1);
    oled.fillRect(40, 28, 1, 5);
    oled.fillRect(123, 28, 1, 5);
    oled.fillRect(40 + lroundf(84 * m.level), 27, 2, 7);
    // Just the numbers under the track end caps (left cap = min, right cap = max). The "min "/
    // "max " words would overrun the 84px track width on the fixed metrics and garble on the panel.
    text(oled, 40, 36, String(m.minMv));
    text(oled, 124, 36, String(m.maxMv), TEXT_ALIGN_RIGHT);
    text(oled, 40, 48, String("cru ") + String(m.cru, 2) + " cal " + String(m.cal, 2));
}

} // namespace sinal
} // namespace pin
