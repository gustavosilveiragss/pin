#pragma once

#include <Arduino.h>

namespace pin {

enum class PlayMode : uint8_t { Loop = 0,
                                Shuffle = 2 }; // os unicos modos que o web tool emite
enum class ItemType : uint8_t { Video = 0,
                                Image = 1 };

struct Item {
    ItemType type = ItemType::Video;
    uint8_t holdSec = 0;
    uint8_t flags = 0; // bit0 = so-in-range (o video de proximidade/handshake)
    uint32_t offset = 0;
    uint32_t length = 0;
    bool inRangeOnly() const { return flags & 0x01; }
};

// Um bundle .pin e um arquivo que o web tool monta e o pin so toca. Layout de bytes:
//
//   offset      size       campo
//   0           4          magic 'P' 'I' 'N' '1'
//   4           1          versao (1, ou 2 com o bloco de proximidade)
//   5           1          modo (0 loop, 2 shuffle)
//   6           1          count (itens, 1..32)
//   7           1          flags (reservado, 0)
//   8           1          ssidLen (tamanho do nome WiFi legado, ignorado, o tool escreve 0)
//   9           ssidLen    ssid (legado, ignorado, o nome do AP e sempre broche-<code>)
//   (so v2)     1          targetLen (0..8, o codigo curto do amigo pareado)
//   (so v2)     targetLen  target (codigo base32 ASCII que o amigo leu no pin dele)
//   ...         count*8    indice, uma entrada de 8 bytes por item
//   ...         ...        payload, os blobs dos itens em sequencia
//
// Entrada do indice (8 bytes): type(1) | holdSec(1) | itemFlags(1) | reservado(1) | length(u32 LE)
//   type 0 = video (um fluxo de frames), 1 = imagem (um frame segurado holdSec segundos).
//   itemFlags bit0 = so-in-range: o video dedicado de handshake, excluido da fila normal e
//   mostrado so enquanto um handshake mutuo de proximidade esta ativo (v1 = 0).
//
// Cada blob e heatshrink(RLE(frames)). O Player descomprime e desenha. Os offsets dos itens nao
// sao guardados: sao a soma corrente dos tamanhos depois do indice, e o bundle so e aceito se
// essa soma cai exatamente no tamanho do arquivo.
class Bundle {
public:
    static constexpr int kMaxItems = 32;
    static constexpr int kMaxTargetLen = 8;
    static constexpr const char* kPath = "/show.pin";

    bool load(const char* path = kPath);
    static bool validate(const char* path); // check estrutural completo antes de promover um upload

    bool empty() const { return count_ == 0; }
    int count() const { return count_; }
    PlayMode mode() const { return mode_; }
    const char* path() const { return kPath; }
    const Item& item(int index) const { return items_[index]; }

    // Proximidade (video de handshake in-range). Ligada so quando o bundle carrega um codigo de
    // amigo valido, um item esta marcado in-range, e pelo menos um item ainda toca na fila normal.
    // Quando desligada todo item toca normal (comportamento v1). Se o item marcado e mesmo tirado
    // da fila e decidido em runtime pelo caller (depende do radio estar ligado), nao aqui.
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
