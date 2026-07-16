#pragma once

#include <Battery.h>
#include <Button.h>
#include <Ssd1306Display.h>

#include "Sinal.h"

namespace pin {

// What the portal (double-click) screen shows. `myCode` is this pin's proximity code: it is
// appended to the AP name (broche-XXXX) and served at GET /codigo so a phone can read it
// without the OLED (matters for the button-less build). Pass "" to omit. friend_/friendCode
// report whether a paired pin was in range, snapshotted before the radio hands to the SoftAP.
struct PortalInfo {
    const char* ssid;
    const char* myCode;
    sinal::FriendState friend_;
    const char* friendCode;
};

// Runs the on-demand upload portal with the SINAL portal screen. Returns true if a valid
// bundle was received.
bool runWifiMode(mrm::Ssd1306Display& display, mrm::Battery& battery, mrm::Button& button, const PortalInfo& info);

} // namespace pin
