#pragma once

// WiFi in permanent AP+STA mode: the setup access point is ALWAYS up, so a
// device whose saved network vanished is still reachable at 192.168.4.1 to be
// re-provisioned. Everything here runs on the loop task.

// Bring up the AP and kick off a background station connect. Does not block on
// the connect — the AP and web server must be reachable immediately.
void setupWiFi();

// Reconnection, connect-edge detection and the diagnostic scan. Call every
// loop() pass.
void handleWifiMaintenance();

// Ask the loop task to (re)connect the station with the current credentials in
// `config` on its next handleWifiMaintenance() pass. Safe to call from the web
// task — it only sets a flag; all radio work stays on the loop task. Used after
// a config save changes the WiFi credentials, so provisioning connects NOW
// instead of waiting for a retry that never fires while the provisioning phone
// is parked on the SoftAP.
void requestWifiConnect();
