#pragma once

// Push-based OTA (ArduinoOTA) + mDNS. This is the DEV-TIME path: you push a
// build from your machine over the LAN with `pio run -t upload` and
// upload_protocol = espota. It is distinct from network/updater.*, which is the
// pull-based path a deployed device uses to fetch its own updates from GitHub.
//
// Idempotent, and a no-op until the station is connected. Called from
// wifi.cpp's connect edge, so it survives reconnects without re-registering
// mDNS services.
void setupOTA();
