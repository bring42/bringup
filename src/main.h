#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "storage.h"

// ═══════════════════════════════════════════════════════════════════════════
// Global state, owned by main.cpp
// ═══════════════════════════════════════════════════════════════════════════
// Declared here and defined once in main.cpp. This is the ordinary Arduino
// shape: main.cpp owns the singletons, every other translation unit declares
// what it needs. Your app code can use these too.

extern AsyncWebServer server;
extern Config config;
extern bool wifiConnected;      // station link state (the setup AP is always up)
extern bool webUiAvailable;     // LittleFS mounted and holding a UI
extern unsigned long lastWifiAttempt;

// ═══════════════════════════════════════════════════════════════════════════
// Authentication
// ═══════════════════════════════════════════════════════════════════════════
// If config.authToken is empty, everything is open — which is the sane default
// on a LAN and the only way first-boot provisioning can work. Once a token is
// set, it is accepted as `Authorization: Bearer <t>`, `X-API-Key: <t>`, or a
// ?token= query parameter (the last for browser testing).
//
// ⚠️ This is plain HTTP on your LAN: the token crosses the wire in the clear
// and is not a defense against anyone already on your network. It stops casual
// access, nothing more.

bool checkAuth(AsyncWebServerRequest* request);
void sendUnauthorized(AsyncWebServerRequest* request);

// Chunked-body assembly guard. AsyncTCP runs every handler's body callback on
// one task and INTERLEAVES the chunks of concurrent requests, so the usual
// per-handler `static String` accumulator is only safe if one body is assembled
// at a time. beginBody() claims the single slot at the first chunk (returns
// false -> send 409); endBody() releases it and MUST be called on every
// terminal path after a successful beginBody(). A client that disconnects
// mid-body self-heals after BodyGuard::kTimeoutMs.
bool beginBody(AsyncWebServerRequest* request);
void endBody(AsyncWebServerRequest* request);
