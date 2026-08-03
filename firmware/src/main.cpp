#include <marmota.h>

#include <Preferences.h>
#include <cstring>

#include "Bundle.h"
#include "Player.h"
#include "Proximity.h"
#include "Sinal.h"
#include "WifiMode.h"

using namespace mrm;
namespace sinal = pin::sinal;

namespace {

constexpr uint8_t kButtonPin = 9; // botao de boot, tambem o botao externo do case
constexpr uint8_t kLedPin = 8;
constexpr uint8_t kBatteryPin = 3; // no do divisor de bateria no ADC
constexpr uint32_t kDecodeClockMhz = 160;
constexpr uint32_t kIdleClockMhz = 80;
constexpr uint32_t kIdlePollMs = 50;
constexpr uint32_t kActivePollMs = 8; // poll denso com a janela do radio aberta (TX a cada 20ms)
constexpr uint32_t kMsPerSec = 1000;
constexpr float kMvPerV = 1000.0f;

// 64 de 255: legivel em ambiente fechado, consumo do painel quase pela metade. Sobe pra luz do dia.
constexpr uint8_t kOledContrast = 64;

constexpr uint32_t kDiagFrameMs = 250;   // o diag e quase estatico, amostra devagar
constexpr uint32_t kBootFailMs = 500;    // cadencia da rachadura na tela de filesystem morto
constexpr uint32_t kSplashMs = 1800;     // animacao de boot, um ciclo inteiro de tela
constexpr uint32_t kReceivedMs = 1000;   // selo de recebido, um ciclo inteiro
constexpr uint32_t kEncounterMs = 2200;  // transicao de chegada do amigo, um ciclo inteiro
constexpr uint32_t kInterruptMs = 700;   // vida da faixa de overlay, cobre entrada, hold e saida
constexpr size_t kBufferBytes = 1024;    // 128x64 / 8, o buffer de pixels do OLED

constexpr uint8_t kClicksWifi = 2; // menu wifi (codigo, status, upload)
constexpr uint8_t kClicksDiag = 5; // diagnostico de bateria
constexpr uint8_t kClicksCal = 2;  // dentro do diag: grava a calibracao

// Fallback trim, sobrescrito pela calibracao por unidade gravada na NVS (diag, 2 cliques).
// O divisor de 100k tem impedancia alta pro sample-and-hold do ADC e a leitura sai ~5% baixa.
constexpr float kBatteryCal = 1.05f;
constexpr const char* kNvsNamespace = "pin";
constexpr const char* kNvsCalKey = "batcal";
constexpr float kDivider = 2.0f;        // 100k/100k
constexpr float kUsbThresholdV = 4.30f; // acima de uma celula real, a leitura e o rail do usb
constexpr float kFullCellV = 4.2f;
constexpr uint8_t kBatterySamples = 32; // aparado dentro do Battery, o ruido nunca chega no badge
// Espera com o radio fechado antes de medir a bateria. Cabe no gap entre janelas (335ms travado,
// 255ms no chase), entao a medida sai no fim do gap, com a celula no mais relaxado do ciclo. O
// update() se limita a cada 5s por dentro, um gap ocasional pulado no chase nao importa. A
// suavizacao e a histerese ficam dentro do Battery.
constexpr uint32_t kSettleMs = 250;

Ssd1306Display display({.contrast = kOledContrast});
Battery battery({.adcPin = kBatteryPin,
                 .divider = kDivider,
                 .samples = kBatterySamples,
                 .calibration = kBatteryCal,
                 .usbThresholdV = kUsbThresholdV});
Button button(kButtonPin);
Led led({.pin = kLedPin});
pin::Bundle bundle;
pin::Player player(display);
pin::Proximity proximity;
Preferences prefs;

void loadCalibration() {
    prefs.begin(kNvsNamespace, true);
    const float cal = prefs.getFloat(kNvsCalKey, 0.0f);
    prefs.end();
    if (cal > 0.5f && cal < 2.0f)
        battery.setCalibration(cal);
}

void saveCalibration(float cal) {
    prefs.begin(kNvsNamespace, false);
    prefs.putFloat(kNvsCalKey, cal);
    prefs.end();
}

bool g_next = false;
bool g_enterWifi = false;
bool g_enterDiag = false;
uint32_t g_lastLoadMs = 0;
bool g_friendShown = false; // true enquanto o video de handshake in-range esta na tela
int g_current = 0;

void pollNav() {
    const uint8_t n = button.clicks();
    if (n == 0)
        return;
    if (n >= kClicksDiag)
        g_enterDiag = true;
    else if (n == kClicksWifi)
        g_enterWifi = true;
    else
        g_next = true;
}

// True quando a tela nao bate mais com a presenca, entao o playback precisa trocar. Preso ao
// active() pra um g_friendShown velho nao forcar uma borda permanente com o radio desligado.
bool friendEdge() {
    return proximity.active() && proximity.present() != g_friendShown;
}

bool gesturePending() {
    return g_next || g_enterWifi || g_enterDiag;
}

bool shouldStop() {
    pollNav();
    proximity.tick(true); // playback ativo: segura o radio aberto, o decode ja domina o consumo
    return gesturePending() || friendEdge();
}

// Dorme ate o prazo mais proximo: a proxima janela do radio, o teto de frame do caller ou o poll
// idle. O botao acorda na hora em qualquer caso (wake de GPIO na borda, aperto e soltura). Com a
// janela aberta o loop fica acordado num poll denso pro TX/RX da janela nao perder a vez.
void idleWait(uint32_t frameMs = 0) {
    if (proximity.inWindow()) {
        delay(kActivePollMs);
        return;
    }
    uint32_t ms = proximity.msUntilNextWindow();
    if (ms == 0)
        ms = kIdlePollMs; // sem agenda de radio, poll idle
    if (frameMs > 0 && frameMs < ms)
        ms = frameMs;
    power::lightSleep(ms, kButtonPin);
}

void pump() {
    pollNav();
    // So mede em estado conhecido: 80MHz, radio fechado agora, sem decode, celula assentada. Sob
    // carga a leitura sai afundada e puxa o badge pra baixo, entao mede so no gap. O gate roda
    // antes do tick() porque o acordar do idle cai na borda da proxima janela, e a medida do fim
    // do gap precisa sair antes de ela reabrir.
    if (proximity.inWindow())
        g_lastLoadMs = millis();
    else if (millis() - g_lastLoadMs >= kSettleMs)
        battery.update();
    proximity.tick();
    led.heartbeat();
}

// Hold estatico: sem chrome pra animar (imagem da fila em hold, imagem in-range), mantem light sleep.
template <typename Keep>
void hold(Keep keep) {
    power::cpuClock(kIdleClockMhz);
    while (keep()) {
        pump();
        idleWait();
    }
}

// Tela SINAL animada que reage a input: redraw a cada kUiFrameMs, pump da nav em todo passo pra
// agenda do radio e cliques nao serem afetados pelo gate de frame. idleWait() com teto de frame
// dorme em light sleep sem passar nem do proximo frame nem da proxima janela do radio.
template <typename Draw, typename Keep>
void animate(Draw draw, Keep keep) {
    power::cpuClock(kIdleClockMhz);
    const uint32_t start = millis();
    uint32_t last = 0;
    while (keep()) {
        pump();
        const uint32_t now = millis();
        if (now - last >= sinal::kUiFrameMs) {
            draw(now - start);
            display.show();
            last = now;
        }
        idleWait(sinal::kUiFrameMs);
    }
}

// Animacao transiente por um tempo fixo, sem navegacao (splash, selo de recebido). O tick da
// proximidade roda aqui pro beacon continuar saindo e o amigo nao ver o link secar durante a
// animacao. So o tick, sem pollNav: um clique durante a animacao continua descartado pelo
// button.reset() do caller, como em toda tela sem navegacao.
template <typename Draw>
void playFor(Draw draw, uint32_t ms) {
    power::cpuClock(kIdleClockMhz);
    const uint32_t start = millis();
    uint32_t last = 0;
    while (millis() - start < ms) {
        led.heartbeat();
        proximity.tick();
        const uint32_t now = millis();
        if (now - last >= sinal::kUiFrameMs) {
            draw(now - start);
            display.show();
            last = now;
        }
        idleWait(sinal::kUiFrameMs);
    }
}

void holdImage(uint8_t seconds) {
    const uint32_t start = millis();
    const uint32_t ms = uint32_t(seconds) * kMsPerSec;
    hold([&] { return millis() - start < ms && !gesturePending() && !friendEdge(); });
}

// Uma imagem in-range e um frame so, entao loop() desenha uma vez e segura aqui em vez de
// redesenhar todo passo (o que apagaria e redesenharia no clock cheio).
void holdWhilePresent() {
    hold([] { return proximity.present() && !gesturePending(); });
}

// Uma faixa transiente sobre o frame de conteudo congelado num clique. O buffer e copiado uma vez
// pra cada frame recompor a faixa sobre a mesma imagem parada, depois o playback avanca.
void overlayFila() {
    uint8_t bg[kBufferBytes];
    uint8_t* buf = display.raw().buffer;
    memcpy(bg, buf, kBufferBytes);
    const uint8_t tracks = min(3, bundle.count());
    const sinal::InterruptModel model{
        .kind = sinal::Overlay::Fila,
        .friendCode = nullptr,
        .track = uint8_t(g_current % max<int>(1, tracks)),
        .trackCount = tracks,
    };
    power::cpuClock(kIdleClockMhz);
    const uint32_t start = millis();
    uint32_t last = 0;
    // pump() mantem a maquina de estado do botao viva pra um clique novo durante a faixa
    // encerrar cedo em vez de ser engolido (senao nao pollava nada o tempo todo da faixa).
    while (millis() - start < kInterruptMs && !gesturePending()) {
        pump();
        const uint32_t now = millis();
        if (now - last >= sinal::kUiFrameMs) {
            memcpy(buf, bg, kBufferBytes);
            sinal::interrupcao(display.raw(), now - start, model);
            display.show();
            last = now;
        }
        idleWait(sinal::kUiFrameMs);
    }
}

// Diagnostico escondido (5 cliques). Mostra a leitura crua do divisor. Com a celula cheia e em
// repouso (fora do carregador), 2 cliques gravam cal = 4.20 / cru na NVS: e o que tira o erro de
// resistor e de ganho de ADC desta unidade. Um clique sai.
void batteryDiag() {
    button.reset(); // descarta a rajada que abriu o diag pra um clique poder sair
    power::cpuClock(kIdleClockMhz);
    float lo = 9999.0f;
    float hi = 0.0f;
    uint32_t last = 0;
    for (;;) {
        const uint8_t n = button.clicks();
        if (n >= kClicksCal) {
            // O limiar vale pra leitura ja calibrada, igual ao Battery: comparar a crua deixaria o
            // rail de 5V passar por celula e gravar uma calibracao envenenada.
            const float cal =
                battery.readNodeMillivolts() * kDivider / kMvPerV * battery.calibration();
            if (cal < kUsbThresholdV) // no usb o no fica no rail de 5V, calibrar ali estraga tudo
                saveCalibration(battery.calibrateTo(kFullCellV));
        } else if (n > 0) {
            break;
        }
        proximity.tick();
        led.heartbeat();
        const uint32_t now = millis();
        if (now - last >= kDiagFrameMs) {
            const float node = battery.readNodeMillivolts(); // mesma leitura aparada do badge
            lo = min(lo, node);
            hi = max(hi, node);
            const float cru = node * kDivider / kMvPerV; // celula, sem calibrar
            const float cal = cru * battery.calibration();
            const sinal::DiagModel model{
                .nodeMv = int(node),
                .minMv = int(lo),
                .maxMv = int(hi),
                .cru = cru,
                .cal = cal,
                .level = gfx::clamp01(cal / kFullCellV),
                .usb = cal > kUsbThresholdV,
            };
            sinal::diag(display.raw(), now, model);
            display.show();
            last = now;
        }
        idleWait(kDiagFrameMs);
    }
    g_enterDiag = false;
}

// O unico item de handshake so sai da fila enquanto o radio esta ligado. Fora isso ele toca como
// qualquer outro item, entao nunca fica silenciosamente inalcancavel.
bool filaSkip(int index) {
    return proximity.active() && index == bundle.inRangeIndex();
}

int firstFilaItem() {
    for (int i = 0; i < bundle.count(); ++i)
        if (!filaSkip(i))
            return i;
    return 0;
}

int filaPlayable() {
    int n = 0;
    for (int i = 0; i < bundle.count(); ++i)
        if (!filaSkip(i))
            ++n;
    return n;
}

void advanceSequential() {
    const int n = bundle.count();
    for (int k = 0; k < n; ++k) {
        g_current = (g_current + 1) % n;
        if (!filaSkip(g_current))
            return;
    }
}

void advanceByMode() {
    if (bundle.mode() == pin::PlayMode::Shuffle && filaPlayable() > 1) {
        int next;
        do {
            next = random(bundle.count());
        } while (next == g_current || filaSkip(next));
        g_current = next;
    } else {
        advanceSequential(); // loop
    }
}

// Depois do playback, age no evento pendente. Prioridade: menu, chegada do amigo, next.
// Um clique unico de avanco pisca a faixa de overlay antes. Retorna true pra reiniciar loop().
bool interrupted() {
    if (g_enterWifi || g_enterDiag)
        return true;
    if (proximity.present())
        return true;
    if (g_next) {
        g_next = false; // consome o clique de avanco antes da faixa, pra ela nao se abortar
        overlayFila();
        if (g_enterWifi || g_enterDiag)
            return true; // um gesto de menu cortou a faixa: despacha ele, nao avanca a fila
        advanceSequential();
        return true;
    }
    return false;
}

// Adota o /show.pin: carrega, reinicia o radio pro alvo dele, rebobina a fila.
void reloadBundle() {
    bundle.load();
    if (proximity.configure(bundle))
        display.reinit(); // o radio subindo perturba a bomba de carga do OLED
    g_current = firstFilaItem();
}

void enterWifi() {
    g_enterWifi = false;
    led.on();
    // Congela o link antes do radio passar pro SoftAP, pra o menu poder reportar.
    sinal::FriendState friendState;
    if (proximity.active())
        friendState = proximity.present() ? sinal::FriendState::Near : sinal::FriendState::Far;
    else if (bundle.proximityEnabled())
        friendState = sinal::FriendState::Error;
    else
        friendState = sinal::FriendState::None;
    const String friendCode = bundle.target();
    proximity.stop(); // o SoftAP precisa do radio em modo AP
    const pin::PortalInfo info{
        .myCode = proximity.myCode(),
        .friend_ = friendState,
        .friendCode = friendCode.c_str(),
    };
    const bool received = pin::runWifiMode(display, battery, button, info);
    g_lastLoadMs = millis();
    led.off();
    reloadBundle(); // o radio volta ja aqui, o beacon corre durante o selo de recebido
    if (received)
        playFor([](uint32_t t) { sinal::recebido(display.raw(), t); }, kReceivedMs);
    button.reset();
    g_next = false;
}

} // namespace

void setup() {
    logBegin();
    led.begin();
    button.begin();
    randomSeed(micros());
    display.begin();
    loadCalibration();
    battery.begin();
    proximity.identify();

    playFor([](uint32_t t) { sinal::splash(display.raw(), t); }, kSplashMs);
    button.reset();

    if (!fsMount()) {
        // Um filesystem quebrado e fatal e nao "sem conteudo", entao para aqui com a tela da
        // rachadura em vez de deixar loop() convidar um upload wifi que nunca vai persistir.
        // Um aparelho morto pode ficar aqui por horas, entao faz light sleep entre os redraws
        // lentos da rachadura em vez de busy-spin. A rachadura so anda duas vezes por segundo.
        power::cpuClock(kIdleClockMhz);
        uint32_t last = 0;
        const uint32_t start = millis();
        for (;;) {
            led.heartbeat();
            const uint32_t now = millis();
            if (now - last >= kBootFailMs) {
                sinal::bootFail(display.raw(), now - start);
                display.show();
                last = now;
            }
            power::lightSleep(kIdlePollMs, kButtonPin);
        }
    }
    reloadBundle();
}

void loop() {
    if (g_enterDiag) {
        batteryDiag(); // volta num clique
        return;
    }
    if (g_enterWifi) {
        enterWifi();
        return;
    }

    if (bundle.empty()) {
        g_friendShown = false;
        g_next = false;
        button.reset();
        animate([](uint32_t t) { sinal::semConteudo(display.raw(), t); },
                [] { return !gesturePending() && !friendEdge(); });
        return;
    }

    // Um handshake ao vivo assume com o item in-range. g_current fica parado pra a fila voltar
    // de onde estava na saida. A chegada toca a transicao de encontro uma vez.
    if (proximity.present() && bundle.proximityEnabled()) {
        if (!g_friendShown) {
            button.reset(); // a rajada em voo do item anterior nao pode virar entrada de menu
            const uint32_t start = millis();
            animate([](uint32_t t) { sinal::encontroEnter(display.raw(), t, {bundle.target().c_str()}); },
                    [&] { return millis() - start < kEncounterMs && proximity.present() && !g_enterWifi && !g_enterDiag; });
            // So sai por gesto de menu ou pelo amigo indo embora. Um clique simples de avanco cai
            // e e consumido abaixo, entao nunca trava o ramo em livelock.
            if (g_enterWifi || g_enterDiag || !proximity.present())
                return;
        }
        g_friendShown = true;
        const pin::Item& item = bundle.item(bundle.inRangeIndex());
        g_next = false;
        button.reset();
        power::cpuClock(kDecodeClockMhz);
        player.play(bundle.path(), item.offset, item.length, shouldStop);
        g_lastLoadMs = millis();
        // Um video repete ao reentrar no loop. Uma imagem e um frame so, entao segura em vez de redesenhar.
        if (item.type == pin::ItemType::Image && !gesturePending() && proximity.present())
            holdWhilePresent();
        return;
    }
    g_friendShown = false;

    const pin::Item& item = bundle.item(g_current);
    g_next = false;
    button.reset(); // um clique completo do item anterior nao pode avancar este

    power::cpuClock(kDecodeClockMhz);
    player.play(bundle.path(), item.offset, item.length, shouldStop);
    g_lastLoadMs = millis();

    if (interrupted())
        return;
    if (item.type == pin::ItemType::Image) {
        holdImage(item.holdSec);
        if (interrupted())
            return;
    }
    advanceByMode();
}
