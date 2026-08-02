#pragma once

#include <ESPAsyncWebServer.h>

// Registers every base route, then calls appRegisterRoutes() so your project's
// endpoints are added last (and can therefore never shadow the base ones), then
// starts the server. Call once from setup(), after setupWiFi().
void setupServer();

// WebSocket housekeeping and the ~1 Hz state broadcast. Call every loop() pass.
void loopServer();

// Push the current status to every connected WebSocket client immediately,
// instead of waiting for the next periodic broadcast. Call from the LOOP task
// when something changed that the UI should see at once.
void broadcastState();
