#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// Root route — serves the web UI from LittleFS.
void handleRoot(AsyncWebServerRequest* request);

// GET /api/status
void handleApiStatus(AsyncWebServerRequest* request);

// Fills `out` with the device status. ONE payload shape, shared by
// GET /api/status and the WebSocket broadcast — so the UI reads the same fields
// whether it polled or was pushed. Calls appBuildStatus() last, letting your
// project add its own fields to the same object.
void buildStatus(JsonObject out);
