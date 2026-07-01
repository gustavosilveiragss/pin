# pin

Wearable ESP32-C3 smart pin. OLED 128x64, LiPo, USB-C charge. Plays a bundle of 1 bit
videos and images. New content over WiFi, no reflash.

## Make content

Web tool: **https://pin.marmota.dev.br** (source in [`docs/`](docs/)). Crop, threshold,
download a `.pin`.

## Load

Double click the button. Pin raises WiFi `broche`. Phone opens the page on its own, or
hit `192.168.4.1`. Upload the `.pin`. Done. One click next, double click WiFi.

## Hardware

- ESP32-C3 SuperMini + SSD1306 OLED (I2C GPIO5/6)
- LiPo 3.7V + TP4056 USB-C + slide switch
- Battery divider 100k/100k on GPIO3
- External button on GPIO9 (works with the case closed), screw-openable case

Wiring in [`CIRCUITO.md`](CIRCUITO.md), schematic in [`perfboard/`](perfboard/).

## Firmware

PlatformIO in [`firmware/`](firmware/). `pio run` builds, `pio run -t upload` flashes an
ESP32-C3. Radios off unless you ask; low clock and light sleep when idle.

## License

MIT, from the Hackerspace-FFM ESP32 player (MIT, 2018). See [`LICENSE`](LICENSE).
