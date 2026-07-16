# pin

Wearable ESP32-C3 smart pin with a 128x64 OLED, a LiPo battery, and USB-C charging. It
plays a small bundle of 1-bit videos and images, and you add new ones over WiFi without
ever reflashing it.

## Make content

The web tool lives at https://pin.marmota.dev.br (source in [docs/](docs/)). Crop your
image or clip, set the threshold, and download a `.pin` file.

## Load it

Double click the button. The pin puts up a WiFi network called `broche`, and your phone
opens the page on its own, or you can go to `192.168.4.1`. Upload the `.pin` and you are
done. After that a single click moves to the next item and a double click brings WiFi
back.

## Hardware

- ESP32-C3 SuperMini and an SSD1306 OLED (I2C on GPIO5 and GPIO6)
- LiPo 3.7V, a TP4056 USB-C charger, and a slide switch
- A 100k/100k divider on GPIO3 reads the battery
- A button on GPIO9 that still works with the case closed

Wiring is in [CIRCUITO.md](CIRCUITO.md), the board layout in [perfboard/](perfboard/), and
the 3D case in [case/](case/).

## Firmware

PlatformIO project in [firmware/](firmware/). Run `pio run` to build and `pio run -t
upload` to flash an ESP32-C3. The radios stay off until you ask for them, and the chip
drops its clock and goes into light sleep when nothing is happening.

## License

MIT. It started from the Hackerspace-FFM ESP32 player (MIT, 2018). See [LICENSE](LICENSE).
