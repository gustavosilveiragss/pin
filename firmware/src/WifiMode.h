#pragma once

#include <Battery.h>
#include <Button.h>
#include <Ssd1306Display.h>

namespace pin {

// Runs the on-demand upload portal with the pin status screen.
// Returns true if a valid bundle was received.
bool runWifiMode(mrm::Ssd1306Display& display, mrm::Battery& battery, mrm::Button& button, const char* ssid);

} // namespace pin
