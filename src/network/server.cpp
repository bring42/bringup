#include "server.h"
#include "../app.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../api/status.h"
#include "../api/config.h"
#include "../api/firmware.h"
#include "updater.h"
#include "timesync.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

static AsyncWebSocket ws("/ws");
static unsigned long lastWsBroadcast = 0;
constexpr uint32_t WS_BROADCAST_INTERVAL_MS = 1000;

static String contentTypeFromPath(const String& path) {
    if (path.endsWith(".html")) return "text/html; charset=utf-8";
    if (path.endsWith(".css"))  return "text/css; charset=utf-8";
    if (path.endsWith(".js"))   return "application/javascript; charset=utf-8";
    if (path.endsWith(".svg"))  return "image/svg+xml";
    if (path.endsWith(".png"))  return "image/png";
    if (path.endsWith(".ico"))  return "image/x-icon";
    if (path.endsWith(".json")) return "application/json; charset=utf-8";
    if (path.endsWith(".txt"))  return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

// ── WebSocket ───────────────────────────────────────────────────────────────
// One payload shape, built by buildStatus() in api/status.cpp, shared with
// GET /api/status. A client gets it on connect and then ~1 Hz thereafter.

static bool buildStatePayload(String& payload) {
    JsonDocument doc;
    doc["type"] = "state";
    JsonObject status = doc["status"].to<JsonObject>();
    buildStatus(status);
    payload.clear();
    serializeJson(doc, payload);
    return payload.length() > 0;
}

static void handleWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
                          AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT && client) {
        String payload;
        if (buildStatePayload(payload)) {
            client->text(payload);
        }
    }
}

void broadcastState() {
    if (ws.count() == 0) return;
    String payload;
    if (buildStatePayload(payload)) {
        ws.textAll(payload);
    }
}

// ── Setup ───────────────────────────────────────────────────────────────────

void setupServer() {
    ws.onEvent(handleWsEvent);
    server.addHandler(&ws);

    if (webUiAvailable) {
        server.serveStatic("/assets/", LittleFS, "/assets/")
            .setCacheControl("public, max-age=604800")
            // A week of caching is safe ONLY because every asset URL carries a
            // ?v=<content-hash> stamp (scripts/gzip_web_files.py) — the URL
            // changes whenever the bytes do.
            //
            // During an OTA filesystem flash the LittleFS partition is mid-erase.
            // Disable static serving then, so the request falls through to the
            // updaterInProgress() 503 guard in onNotFound instead of reading a
            // partition being actively overwritten (garbage/fault -> FS corruption).
            .setFilter([](AsyncWebServerRequest*) { return !bringup::updaterInProgress(); });
        LOG_INFO(LogTag::WEB, "Serving UI assets from LittleFS");
    } else {
        LOG_WARN(LogTag::WEB, "LittleFS not mounted; UI assets unavailable");
    }

    server.on("/", HTTP_GET, handleRoot);

    // Lightweight health check for monitoring. Deliberately unauthenticated and
    // cheap — it is what you point an uptime checker at.
    server.on("/health", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;

        doc["status"]  = "healthy";
        doc["uptime"]  = millis() / 1000;
        doc["version"] = FIRMWARE_VERSION;

        JsonObject memory = doc["memory"].to<JsonObject>();
        memory["heap_free"]      = ESP.getFreeHeap();
        memory["heap_min"]       = ESP.getMinFreeHeap();
        memory["heap_max_block"] = ESP.getMaxAllocHeap();

        // Fragmentation: free heap that is NOT in the largest contiguous block.
        // A high number with plenty of free heap is what precedes the "out of
        // memory with 40 KB free" allocation failures.
        uint32_t heapFree = ESP.getFreeHeap();
        uint32_t maxBlock = ESP.getMaxAllocHeap();
        if (heapFree > 0) {
            memory["fragmentation"] = 100 - (maxBlock * 100 / heapFree);
        }

        JsonObject network = doc["network"].to<JsonObject>();
        network["wifi_connected"] = wifiConnected;
        network["wifi_rssi"]      = wifiConnected ? WiFi.RSSI() : 0;
        network["ip"]             = wifiConnected ? WiFi.localIP().toString()
                                                  : WiFi.softAPIP().toString();
        network["ap_clients"]     = WiFi.softAPgetStationNum();

        JsonObject components = doc["components"].to<JsonObject>();
        components["storage"] = storage.isReady();
        components["web_ui"]  = webUiAvailable;
        // enabled = SNTP compiled in; valid = the clock is usable however it got
        // set. A monitor should treat enabled-but-not-valid as degraded, and
        // not-enabled as fine.
        components["time_enabled"] = (bool)BRINGUP_TIME_SYNC;
        components["time_valid"]   = bringup::timeValid();

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // Status + configuration
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/api/config", HTTP_GET, handleApiConfig);
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handleApiConfigPost
    );

    // Self-update (pull-based OTA from GitHub Releases). Check and update are
    // async — the worker does the blocking HTTPS transfer and the UI polls
    // status. Check is unified (reports firmware and filesystem); apply is
    // atomic by default, with per-image endpoints kept for recovery.
    server.on("/api/firmware/check",     HTTP_POST, handleApiFirmwareCheck);
    server.on("/api/firmware/status",    HTTP_GET,  handleApiFirmwareStatus);
    server.on("/api/firmware/update",    HTTP_POST, handleApiFirmwareUpdate);     // atomic (UI)
    server.on("/api/firmware/update/app", HTTP_POST, handleApiFirmwareUpdateApp); // recovery
    server.on("/api/firmware/update/fs",  HTTP_POST, handleApiFirmwareUpdateFs);  // recovery

    // ── Your routes, registered last so they can't shadow the base ones ──
    appRegisterRoutes(server);

    // CORS preflight
    server.on("/api/*", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key");
        request->send(response);
    });

    // 404 handler with SPA fallback
    server.onNotFound([](AsyncWebServerRequest* request) {
        String path = request->url();
        if (path.startsWith("/api/")) {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
            return;
        }

        if (!webUiAvailable) {
            request->send(404, "text/plain", "Not found");
            return;
        }

        // Don't read LittleFS while an OTA is overwriting the FS partition.
        if (bringup::updaterInProgress()) {
            request->send(503, "text/plain", "Firmware update in progress");
            return;
        }

        if (path.length() == 0) {
            path = "/";
        }
        if (!path.startsWith("/")) {
            path = "/" + path;
        }
        if (path.endsWith("/")) {
            path += "index.html";
        }

        if (LittleFS.exists(path)) {
            AsyncWebServerResponse* response =
                request->beginResponse(LittleFS, path, contentTypeFromPath(path));
            // HTML entry points must revalidate: they carry the ?v= cache-busted
            // asset URLs, so a cached page would pin clients to an old asset set.
            if (path.endsWith(".html")) {
                response->addHeader("Cache-Control", "no-cache");
            }
            request->send(response);
            return;
        }

        // SPA fallback: serve index for client-side routes without extensions.
        if (path.indexOf('.') < 0 && LittleFS.exists("/index.html")) {
            AsyncWebServerResponse* response =
                request->beginResponse(LittleFS, "/index.html", "text/html; charset=utf-8");
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
            return;
        }

        request->send(404, "text/plain", "Not found");
    });

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    server.begin();
    LOG_INFO(LogTag::WEB, "Web server started on port 80");
}

void loopServer() {
    ws.cleanupClients();

    if (ws.count() == 0) {
        return;
    }

    unsigned long now = millis();
    if (now - lastWsBroadcast >= WS_BROADCAST_INTERVAL_MS) {
        lastWsBroadcast = now;
        broadcastState();
    }
}
