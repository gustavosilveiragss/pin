# Circuito do pin

Broche ESP32-C3 com OLED, bateria LiPo e carga USB-C. Esquema visual em [`perfboard/`](perfboard/).

## Componentes

| Item | Detalhe |
|---|---|
| ESP32-C3 SuperMini | placa principal |
| OLED SSD1306 0.96" I2C | tela 128x64 |
| TP4056 com protecao DW01 | carga USB-C |
| LiPo 3.7V 1500mAh | com PCB de protecao |
| Chave SS-12D00 | liga e desliga |
| Botao momentario | na case, ligado no boot (IO9) |
| 2x resistor 100k | divisor pra ler a bateria |
| 1x capacitor 100nF | do no do divisor pro GND, junto do IO3 |
| Alfinete de broche | fixacao |

## Pinos do ESP32-C3

| Pino | Funcao |
|---|---|
| 5V | entrada de energia, vinda da chave |
| GND | terra comum |
| 3V3 | alimenta o OLED |
| IO5 | OLED SDA |
| IO6 | OLED SCL |
| IO3 | leitura da bateria (no do divisor) |
| IO9 | botao, junto do boot da placa |

## Ligacoes

### Carga e energia

| De | Para |
|---|---|
| Bateria + | TP4056 B+ |
| Bateria menos | TP4056 B menos |
| TP4056 OUT+ | chave, pino central |
| chave, pino lateral | ESP 5V |
| TP4056 OUT menos | ESP GND |

USB-C no TP4056 carrega a bateria. LED vermelho carregando, verde cheio. A chave em serie no OUT+ liga e desliga o broche.

### OLED (jumpers femea femea)

| OLED | ESP |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | IO5 |
| SCL | IO6 |

O OLED ja tem pull up I2C na propria placa, entao o barramento nao precisa de resistor.

### Botao (funciona com a case fechada)

| De | Para |
|---|---|
| IO9 | um lado do botao |
| GND | outro lado do botao |

Botao momentario, em paralelo com o boot da placa, levado pra fora na case. Um clique passa pro proximo item, dois cliques abrem o WiFi, cinco abrem o diagnostico de bateria. Pra desligar de verdade, a chave.

### Divisor da bateria

| De | Para |
|---|---|
| 5V | R1 de 100k |
| R1 e R2 | no comum |
| no comum | IO3 |
| no comum | capacitor 100nF |
| capacitor 100nF | GND |
| R2 de 100k | GND |

O no fica na metade da tensao da bateria, entao o IO3 le Vbat dividido por 2. O IO3 e do ADC1, que funciona com o WiFi ligado.

O divisor de 100k tem impedancia alta (50k no no) pro sample and hold do ADC, o que puxa a leitura pra baixo. O capacitor de 100nF do no pro GND segura a carga entre as conversoes e mata o ruido do radio, entao ele nao e opcional: o firmware assume que ele esta la (`kBatteryCal = 1.0`). Se montar sem o cap, a leitura sai ~5% baixa e a calibracao tem que compensar isso.

### Calibracao da bateria (por unidade)

Resistor de 5% e ganho de ADC variam de placa pra placa: sem calibrar, o mesmo 3.7V real pode virar 3.5V num broche e 3.9V no outro. O firmware guarda uma constante por unidade na NVS.

1. carregue ate o LED verde do TP4056
2. tire o USB e espere ~1 minuto (a celula relaxa de 4.25 pra ~4.20V)
3. ligue na chave, 5 cliques abrem o diagnostico
4. 2 cliques gravam `cal = 4.20 / cru`. O numero em `cal` na tela passa a bater com 4.20
5. um clique sai

Feito uma vez por broche. Enquanto estiver no USB o diag recusa gravar (o no fica no rail de 5V).

O firmware so le a bateria em idle (80MHz, radio desligado). Durante video, encontro ou portal WiFi ele mostra a ultima leitura boa, em vez de uma leitura afundada pelo consumo.

Carregando com a chave ligada o numero mente: o TP4056 segura o terminal em 4.2V e o broche mostra cheio mesmo com a celula pela metade. Sem fio no pino CHRG nao da pra detectar isso, entao a regra e: leia a porcentagem fora do carregador.

A porcentagem so aparece no menu wifi (2 cliques). A curva e de tensao em repouso (OCV) de LiPo a 25C, com 0% em 3.30V, que e onde o LDO da placa larga o 3V3. Mesmo calibrada, no meio da descarga a curva e quase plana (3.75-3.85V cobre de 25% a 55%), entao trate o numero como faixa, nao como medida: cheio e vazio ele acerta, o meio e chute educado.

## Consumo e autonomia

Bateria de 1500mAh entrega ~1400mAh uteis (celula real abaixo do nominal, corte em 3.0V). Valores medios estimados, ainda sem bancada.

| Estado | Consumo | Autonomia |
|---|---|---|
| Video rodando (160MHz) | ~25-33mA | 40-55h |
| Tela animada parada | ~7-13mA | 110-200h |
| Imagem estatica em hold | ~4-9mA | 150-350h |
| Encontro ligado (ESP-NOW) | ~22-35mA | 40-60h |
| Portal WiFi aberto | ~90-115mA | 12-15h |

Desligado na chave o dreno e so o TP4056 em standby (~5-10uA), entao guardar o broche nao custa carga.

O que mais pesa depois disso e o LED vermelho de power do SuperMini: fica aceso sempre, ~1mA. Dessoldar ele ganha ~10-20% na tela parada e nao muda mais nada.

Os 160MHz do decode ficam como estao: baixar pra 80MHz economiza ~7-10mA mas arrisca o framerate do video, e com 1500mAh nao faz falta.
