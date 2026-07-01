# Circuito do pin

Broche ESP32-C3 com OLED, bateria LiPo e carga USB-C. Esquema visual em [`perfboard/`](perfboard/).

## Componentes

| Item | Detalhe |
|---|---|
| ESP32-C3 SuperMini | placa principal |
| OLED SSD1306 0.96" I2C | tela 128x64 |
| TP4056 com protecao DW01 | carga USB-C |
| LiPo 3.7V 400mAh | com PCB de protecao |
| Chave SS-12D00 | liga e desliga |
| Botao momentario | na case, ligado no boot (IO9) |
| 2x resistor 100k | divisor pra ler a bateria |
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

Botao momentario, em paralelo com o boot da placa, levado pra fora na case. Um clique passa pro proximo item, dois cliques abrem o WiFi.

### Divisor da bateria

| De | Para |
|---|---|
| 5V | R1 de 100k |
| R1 e R2 | no comum |
| no comum | IO3 |
| R2 de 100k | GND |

O no fica na metade da tensao da bateria, entao o IO3 le Vbat dividido por 2. O IO3 e do ADC1, que funciona com o WiFi ligado.
