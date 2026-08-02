#pragma once

#include <ESPAsyncWebServer.h>

// GET  /api/config  -> current config, secrets masked
// POST /api/config  -> merge the posted fields, persist, apply
//
// The POST is how WiFi provisioning happens: saving new credentials asks the
// loop task to connect with them immediately, because the periodic retry is
// (deliberately) suppressed while the provisioning phone sits on the setup AP.
void handleApiConfig(AsyncWebServerRequest* request);
void handleApiConfigPost(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                         size_t index, size_t total);
