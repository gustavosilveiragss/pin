#pragma once

#include <Battery.h>
#include <Button.h>
#include <Ssd1306Display.h>

namespace pin {

// Runs the on-demand upload portal with the pin status screen. `myCode` is this pin's
// proximity code: it is appended to the AP name (broche-XXXX) and served at GET /codigo so
// a phone can read it without the OLED (matters for the button-less build). Pass "" to omit.
// `proxStatus`, if non-empty, is shown as the first status line (e.g. "amigo XR4T: perto")
// so the double-click menu tells you whether a paired pin was in range. Returns true if a
// valid bundle was received.
bool runWifiMode(mrm::Ssd1306Display& display, mrm::Battery& battery, mrm::Button& button, const char* ssid, const char* myCode, const char* proxStatus);

} // namespace pin
