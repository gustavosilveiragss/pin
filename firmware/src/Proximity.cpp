#include "Proximity.h"
#include "Bundle.h"
#include "Code.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

namespace pin {

namespace {

constexpr uint8_t kChannel = 1; // todo pin fixa o mesmo canal (sem scan/hop)

constexpr uint8_t kMagic = 0xB7;
constexpr uint8_t kVersion = 0x03; // v3: beacon carrega a fase ate a proxima janela do sender

// beacon de broadcast de 12 bytes. flags bit0 = sender ouve o alvo dele fresco (o ack do
// handshake), bit1 = sender tem alvo. phase = ms desta transmissao ate a proxima janela do
// sender, so o beacon do master ancora o receptor.
struct __attribute__((packed)) Beacon {
    uint8_t magic;
    uint8_t version;
    uint8_t flags;
    uint8_t sender[3]; // codigo de 20 bits
    uint8_t target[3]; // codigo de 20 bits, 0 quando nao configurado
    uint8_t seq;
    uint8_t phase[2];
};
static_assert(sizeof(Beacon) == 12, "beacon wire size");

// Janela e cadencias do TDMA. Na descoberta o radio fica continuo (discovering()), entao capturar
// o primeiro beacon do amigo nao depende da sobreposicao estreita do slide, cai em ate um periodo
// do master. O slide (chase abre a cada kChasePeriodMs, passo kDiscPeriodMs - kChasePeriodMs = 80ms)
// serve so como agenda de fase pro chase travar. Travado, os dois seguem a grade do master e o duty
// religa por janela pra economia.
constexpr uint32_t kWinMs = 85;          // largura da janela de radio aberta no regime travado
constexpr uint32_t kDiscPeriodMs = 420;  // cadencia do master, e do chase travado
constexpr uint32_t kChasePeriodMs = 340; // cadencia do chase destravado (o passo do varrido)
constexpr uint32_t kTxGapMs = 20;        // espaco entre beacons dentro da janela aberta

// Adianta a ancora do chase pra compensar a latencia entre a fase medida no send do master e o
// callback de RX aqui, e abrir um tico antes da janela dele.
constexpr uint32_t kChaseLeadMs = 5;

// Janela de frescor do handshake. Tolera duas janelas perdidas (a terceira chega em ate
// 3 * 420 - 85 = 1175ms), pra um corpo passando na frente nao piscar o link, e cai em ~1.3s
// quando o amigo some, os dois lados dentro de ~1 janela um do outro.
constexpr uint32_t kFreshMs = 1300;

// Silencio que derruba a trava do chase, ~6 janelas. Maior que kFreshMs de proposito: num blip
// curto o present ja caiu mas a agenda segue na grade do master e religa sem varrer.
constexpr uint32_t kLockLossMs = 2500;

// Rajada de radio continuo na descoberta: cobre o boot e o tempo ouvindo o amigo antes do present
// fechar. Depois disso, sozinho, cai no duty pra economizar ate ouvir de novo.
constexpr uint32_t kDiscoverMs = 12000;

uint32_t rd20(const uint8_t* p) {
    return (uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | uint32_t(p[2])) & 0xFFFFF;
}

void wr20(uint8_t* p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
}

uint32_t rd16(const uint8_t* p) {
    return uint32_t(p[0]) << 8 | uint32_t(p[1]);
}

void wr16(uint8_t* p, uint32_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

} // namespace

Proximity* Proximity::s_active = nullptr;

void Proximity::identify() {
    if (identified_)
        return;
    const uint64_t mac = ESP.getEfuseMac() & 0xFFFFFFFFFFFFULL;
    uint32_t x = uint32_t(mac ^ (mac >> 24) ^ (mac >> 45));
    x *= 0x9E3779B1u; // mistura multiplicativa de Knuth pra MACs de fabrica vizinhos se espalharem
    uint32_t u = (x >> 12) & 0xFFFFF;
    if (u == 0)
        u = 1; // 0 e o sentinela de "nao configurado" no fio
    myUid_ = u;
    code::encode(u, myCode_);
    identified_ = true;
}

// Retorna true se o RF foi ligado, entao o caller precisa reiniciar a bomba de carga do OLED.
// Retorna false quando a proximidade nunca comecou (feature off ou um alvo inutil) e o radio
// ficou intocado.
bool Proximity::configure(const Bundle& bundle) {
    stop();
    identify();
    if (!bundle.proximityEnabled())
        return false;

    myTarget_ = code::decode(bundle.target());
    if (myTarget_ == 0 || myTarget_ == myUid_)
        return false; // indecodificavel, ou nosso proprio codigo (nunca casa) => fica off, item na fila

    // O estado do handshake nasce stale: lastRxMs_ recuado em kFreshMs deixa now - lastRxMs_ ja
    // fora da janela desde o primeiro tick (a aritmetica unsigned modular cobre o underflow perto
    // do boot).
    lastRxMs_ = millis() - kFreshMs;
    peerAcks_ = 0;
    peerPhase_ = 0;
    syncSeenMs_ = lastRxMs_;
    present_ = false;
    lastTxMs_ = 0;

    // Daqui o RF esta ligado (bomba de carga do OLED perturbada), entao retorna true. O driver
    // WiFi e o ESP-NOW sobem uma vez aqui e duram a sessao, so o RF cicla por janela pra economia.
    // setSleep(false) fixa o power save do wrapper em NONE: sem isso o handler de STA_START
    // reaplica MIN_MODEM a cada esp_wifi_start e o modem-sleep engole o RX do beacon na janela.
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    // O C3 sobe o STA sem ligar o RX do ESP-NOW no boot frio: o TX sai (esp_now_send OK) mas o RX
    // nunca dispara. setTxPower reconfigura o RF e destrava o RX. 8.5dBm cobre a proximidade de
    // sobra e poupa bateria.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    s_active = this; // o callback estatico roteia por aqui, setado antes do espNowUp registrar ele
    espNowUp();

    // A comparacao e simetrica, os dois pins concordam nos papeis sem negociar.
    master_ = myUid_ < myTarget_;
    locked_ = false;

    // Abre a primeira janela agora e mantem o radio continuo pela rajada de descoberta (o
    // fechamento so volta depois que o par fechar ou a rajada expirar). O chase destravado desliza
    // com o periodo menor so como agenda de fase, o RX continuo e quem fecha a descoberta.
    radioUp();
    open_ = true;
    openedAtMs_ = millis();
    discoverUntilMs_ = openedAtMs_ + kDiscoverMs;
    periodMs_ = schedulePeriod();
    nextOpenMs_ = openedAtMs_ + periodMs_;

    active_ = true;
    return true;
}

void Proximity::stop() {
    if (active_) {
        if (open_)
            radioDown(); // fecha a janela corrente (RF)
        espNowDown();
        if (s_active == this)
            s_active = nullptr;
        WiFi.mode(WIFI_OFF); // desmonta o driver que o configure subiu
    }
    active_ = false;
    present_ = false;
    open_ = false;
}

// True se o ultimo beacon mutuo do amigo chegou dentro da janela de frescor. Compara com sinal
// porque o callback da task WiFi pode gravar lastRxMs_ um tico a frente do now amostrado no tick.
bool Proximity::hearsFresh(uint32_t now) const {
    return int32_t(now - lastRxMs_) < int32_t(kFreshMs);
}

// Fase de descoberta: enquanto o par nao fechou, segura o radio continuo pela rajada do boot e por
// mais uma rajada a cada beacon ouvido, pra a captura nao depender da sobreposicao estreita do
// slide. Fechado o present, ou sozinho depois da rajada, cai no duty por janela pra economia.
bool Proximity::discovering(uint32_t now) const {
    if (present_)
        return false;
    return int32_t(now - discoverUntilMs_) < 0 || int32_t(now - lastRxMs_) < int32_t(kDiscoverMs);
}

// Ritmo vigente na abertura da janela. Master e chase travado andam na cadencia de descoberta, o
// chase destravado desliza mais rapido pra cruzar a janela do master.
uint32_t Proximity::schedulePeriod() const {
    if (!master_ && !locked_)
        return kChasePeriodMs;
    return kDiscPeriodMs;
}

// Chase: adota a fase do ultimo beacon do master (escritos pelo caminho de RX). O par
// (lastRxMs_, peerPhase_) nao e atomico, um tear desloca uma janela e o proximo beacon corrige.
void Proximity::consumeSync(uint32_t now) {
    if (master_)
        return;
    const uint32_t rxAt = lastRxMs_;
    if (rxAt == syncSeenMs_)
        return;
    syncSeenMs_ = rxAt;
    locked_ = true;
    uint32_t next = rxAt + peerPhase_ - kChaseLeadMs;
    if (int32_t(next - now) < 0 || int32_t(next - now) > int32_t(2 * kDiscPeriodMs))
        next = now; // fase fora de faixa (beacon corrompido): abre agora, reancora no proximo
    nextOpenMs_ = next;
}

// Monta o ESP-NOW uma vez por sessao, depois do RF de pe. O RX sobrevive ao stop/start do RF por
// janela, entao init, callback e peer nao precisam voltar a cada janela.
void Proximity::espNowUp() {
    if (esp_now_init() != ESP_OK)
        return;
    esp_now_register_recv_cb(&Proximity::onRecv);
    esp_now_peer_info_t peer = {};
    memset(peer.peer_addr, 0xFF, 6);
    peer.channel = kChannel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer); // sem o peer so o send falha, o RX independe dele
}

void Proximity::espNowDown() {
    esp_now_unregister_recv_cb();
    esp_now_deinit();
}

// Liga o RF pra janela. O start reseta o canal, refixa. Na primeira janela o RF ja subiu no
// configure e o esp_wifi_start e no-op.
void Proximity::radioUp() {
    esp_wifi_start();
    esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
}

void Proximity::radioDown() {
    esp_wifi_stop();
}

void Proximity::sendBeacon(uint32_t now) {
    Beacon b;
    b.magic = kMagic;
    b.version = kVersion;
    // bit0 e o ack: este pin ouve o alvo dele fresco. O amigo so liga o present dele com esse
    // bit, entao o handshake exige o link nas duas direcoes.
    b.flags = uint8_t((hearsFresh(now) ? 0x01 : 0x00) | 0x02);
    wr20(b.sender, myUid_);
    wr20(b.target, myTarget_);
    b.seq = seq_++;
    // Fase medida no send: com a janela aberta nextOpenMs_ ja aponta pra proxima, sempre futuro.
    const int32_t left = int32_t(nextOpenMs_ - now);
    wr16(b.phase, left > 0 ? uint32_t(left) : 0);
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(bcast, reinterpret_cast<const uint8_t*>(&b), sizeof(b));
}

void Proximity::tick(bool keepOpen) {
    if (!active_)
        return;
    const uint32_t now = millis();
    consumeSync(now);
    if (!master_ && locked_ && int32_t(now - lastRxMs_) > int32_t(kLockLossMs))
        locked_ = false; // perdeu o master, volta a deslizar

    // present_ exige as duas direcoes: eu ouco o amigo fresco E o ultimo beacon dele confirma que
    // ele tambem me ouve fresco. Os dois lados sobem juntos dentro de uma janela alinhada e caem
    // juntos quando qualquer direcao seca (o ack do outro lado zera, ou o silencio passa da
    // janela de frescor). Uma janela perdida nao flapa, o frescor cobre duas cadencias.
    present_ = hearsFresh(now) && peerAcks_ != 0;

    if (int32_t(now - nextOpenMs_) >= 0) {
        // Inicio nominal da grade: rearma a proxima pelo ritmo vigente e abre (ou segue aberta).
        periodMs_ = schedulePeriod();
        openedAtMs_ = nextOpenMs_;
        nextOpenMs_ += periodMs_;
        while (int32_t(now - nextOpenMs_) >= 0) {
            openedAtMs_ = nextOpenMs_; // um stall longo pega a janela corrente, nao a atrasada
            nextOpenMs_ += periodMs_;
        }
        // So abre se o now ainda cabe na largura da janela do slot corrente. Um stall que ja passou
        // de kWinMs pula pro proximo slot, senao subiria e desceria o radio no mesmo tick sem TX.
        if (!open_ && now - openedAtMs_ < kWinMs) {
            radioUp();
            open_ = true;
        }
    } else if (keepOpen && !open_) {
        // Playback segura o radio aberto fora da grade. A grade nao anda por isso, e o inicio
        // recuado faz a janela fechar assim que o keepOpen soltar.
        radioUp();
        open_ = true;
        openedAtMs_ = now - kWinMs;
    }

    // Na descoberta a janela nao fecha: o radio segue continuo pro RX nao perder o beacon numa
    // sobreposicao curta. Travado ou sozinho depois da rajada, fecha e o duty economiza.
    if (open_ && !keepOpen && !discovering(now) && now - openedAtMs_ >= kWinMs) {
        radioDown();
        open_ = false;
    }

    if (open_ && now - lastTxMs_ >= kTxGapMs) {
        sendBeacon(now);
        lastTxMs_ = now;
    }
}

uint32_t Proximity::msUntilNextWindow() const {
    if (!active_ || open_)
        return 0;
    const int32_t left = int32_t(nextOpenMs_ - millis());
    return left > 0 ? uint32_t(left) : 1;
}

void Proximity::handleRx(const uint8_t* data, int len) {
    if (!active_)
        return; // um callback em voo durante o stop() nao escreve estado
    if (len < int(sizeof(Beacon)))
        return;
    const Beacon* b = reinterpret_cast<const Beacon*>(data);
    if (b->magic != kMagic || b->version != kVersion)
        return;
    if (rd20(b->sender) != myTarget_ || rd20(b->target) != myUid_)
        return;
    // O payload antes do timestamp: um tick que ve o lastRxMs_ fresco le campos no minimo tao novos.
    peerAcks_ = uint32_t(b->flags & 0x01);
    peerPhase_ = rd16(b->phase);
    lastRxMs_ = millis();
}

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void Proximity::onRecv(const struct esp_now_recv_info*, const uint8_t* data, int len) {
#else
void Proximity::onRecv(const uint8_t*, const uint8_t* data, int len) {
#endif
    Proximity* p = s_active; // copia local, o stop() pode zerar durante o voo
    if (p)
        p->handleRx(data, len);
}

} // namespace pin
