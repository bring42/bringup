#include "config.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../network/wifi.h"
#include <ArduinoJson.h>

// Body accumulator for the chunked POST. Safe only because beginBody() grants
// one body at a time — see BodyGuard.
static String configBodyBuffer;

void handleApiConfig(AsyncWebServerRequest* request) {
    JsonDocument doc;
    storage.configToJson(config, doc, true);   // masked

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleApiConfigPost(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                         size_t index, size_t total) {
    if (index == 0) {
        if (!checkAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        if (!beginBody(request)) {
            request->send(409, "application/json", "{\"error\":\"Busy, retry\"}");
            return;
        }
        configBodyBuffer = "";
        if (total > MAX_REQUEST_BODY_SIZE) {
            endBody(request);
            request->send(413, "application/json", "{\"error\":\"Request body too large\"}");
            return;
        }
    }

    // Length-aware: the chunk isn't NUL-terminated, so String((char*)data) would
    // strlen past `len` into adjacent memory.
    configBodyBuffer += String((char*)data, len);

    if (index + len >= total) {
        endBody(request);

        JsonDocument doc;
        if (deserializeJson(doc, configBodyBuffer)) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        // Track whether the WiFi credentials changed, so the loop task can try
        // them right away — nothing else would trigger a connect, and the
        // periodic retry is correctly suppressed while the provisioning client
        // sits on the setup AP.
        String prevSsid = config.wifiSSID;
        String prevPass = config.wifiPassword;

        storage.configFromJson(config, doc);

        bool wifiCredsChanged = (config.wifiSSID != prevSsid ||
                                 config.wifiPassword != prevPass);

        if (!storage.saveConfig(config)) {
            request->send(500, "application/json", "{\"error\":\"Failed to save\"}");
            return;
        }

        if (wifiCredsChanged && config.wifiSSID.length() > 0) {
            // Only sets a flag. Radio calls must not happen on this (AsyncTCP)
            // task — handleWifiMaintenance() picks it up on the loop task.
            LOG_INFO(LogTag::WEB, "WiFi credentials updated; connect requested");
            requestWifiConnect();
        }

        request->send(200, "application/json", "{\"success\":true}");
    }
}
