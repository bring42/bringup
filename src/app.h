#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ═══════════════════════════════════════════════════════════════════════════
// THE APP HOOKS — this is your project's seam into the base.
// ═══════════════════════════════════════════════════════════════════════════
//
// Everything else in src/ is plumbing you should rarely need to touch: WiFi
// provisioning, the web server, NVS config, and both OTA paths. Your project
// lives in app.cpp, behind these five functions. The base calls them; you fill
// them in.
//
// All of them run on the LOOP task except appRegisterRoutes(), which runs once
// during setup, and the request handlers you register inside it (those run on
// the AsyncTCP task — see the warning there).

// Called once at the end of setup(), after storage, WiFi and the web server are
// up. Initialize your hardware here.
void appSetup();

// Called every loop() pass. Keep it non-blocking: the watchdog is armed
// (WATCHDOG_TIMEOUT_SEC) and the async web server shares this core. Anything
// that blocks for more than a few ms belongs on its own FreeRTOS task.
void appLoop();

// Register your HTTP routes. Called once from setupServer(), after the base
// routes (/, /health, /api/status, /api/config, /api/firmware/*) are in place —
// so you cannot accidentally shadow them.
//
// ⚠️ Handlers registered here run on the AsyncTCP task, NOT the loop task.
// Do not touch hardware, open sockets, or block in them. The pattern the base
// uses everywhere: validate the request, hand the work to the loop task (a
// flag, a queue) or a worker task, and return immediately. See
// requestWifiConnect() for the smallest example, and network/updater.cpp for a
// full worker.
void appRegisterRoutes(AsyncWebServer& server);

// Add your fields to the device status object. Called for BOTH GET /api/status
// and the ~1 Hz WebSocket broadcast, so the UI sees the same shape whether it
// polled or was pushed — add a field once and both paths carry it.
//
// Called from the AsyncTCP task (polling) and the loop task (broadcast). Read
// only word-sized/atomic state here, or snapshot it under a lock; do not do
// real work.
void appBuildStatus(JsonObject status);

// Called on the loop task each time the station connects (boot and every
// reconnect). Start anything that needs the network: MQTT, NTP, a UDP
// listener. Must tolerate being called more than once.
void appOnNetworkUp();

// Called on the loop task when the station drops. Stop what appOnNetworkUp()
// started. The setup AP stays up regardless, so the web UI remains reachable.
void appOnNetworkDown();

// Called just before an OTA update begins writing flash (both the push and pull
// paths). Quiesce: stop driving outputs, park anything moving, mute anything
// loud. The device reboots shortly after, and the watchdog is disabled for the
// duration of the flash write.
void appOnUpdateStarting();
