#include "status.h"
#include "../app.h"
#include "../main.h"
#include "../constants.h"
#include "../logging.h"
#include "../storage.h"
#include "../network/updater.h"
#include "../network/timesync.h"
#include <LittleFS.h>
#include <WiFi.h>

void handleRoot(AsyncWebServerRequest* request) {
    // Don't read LittleFS while an OTA is overwriting the FS partition.
    if (bringup::updaterInProgress()) {
        request->send(503, "text/plain", "Firmware update in progress");
        return;
    }
    if (webUiAvailable && LittleFS.exists("/index.html")) {
        AsyncWebServerResponse* response =
            request->beginResponse(LittleFS, "/index.html", "text/html; charset=utf-8");
        // no-cache: the page carries the ?v= cache-busted asset URLs, so a
        // cached copy would pin clients to an old asset set (the /assets/
        // files themselves are cached for a week by design).
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
        return;
    }
    request->send(503, "text/plain",
                  "Web UI not available — run `pio run -t uploadfs`");
}

void buildStatus(JsonObject out) {
    out["name"]      = config.deviceName;
    out["version"]   = FIRMWARE_VERSION;
    out["buildHash"] = FIRMWARE_BUILD_HASH;
    out["boardId"]   = BRINGUP_BOARD_ID;
    out["uptime"]    = millis() / 1000;
    out["heap"]      = ESP.getFreeHeap();
    // Internal die temperature. Not a calibrated ambient reading — it runs well
    // above room temperature by design — but the TREND is what matters: a board
    // that climbs and stays climbing is usually a busy-loop or an unused
    // peripheral left clocked, not a hardware fault.
    out["tempC"]     = temperatureRead();

    JsonObject wifi = out["wifi"].to<JsonObject>();
    wifi["connected"] = wifiConnected;
    // The CONFIGURED SSID, not the connected one: the setup page needs to show
    // "trying to reach <ssid>" while the device is still AP-only.
    wifi["ssid"]      = config.wifiSSID;
    wifi["apClients"] = WiFi.softAPgetStationNum();
    // rssi only while connected: the UI treats its presence as "has signal"
    // (a literal 0 would render as a full-strength "0 dBm").
    if (wifiConnected) {
        wifi["rssi"] = WiFi.RSSI();
    }

    out["ip"] = wifiConnected ? WiFi.localIP().toString()
                              : WiFi.softAPIP().toString();

    // Wall clock. Reported even when disabled, so the UI can distinguish
    // "no clock configured" from "configured but never synced" — the second is
    // a fault worth seeing, the first is just a choice.
    JsonObject t = out["time"].to<JsonObject>();
    t["enabled"] = (bool)BRINGUP_TIME_SYNC;   // is SNTP compiled in
    t["valid"]   = bringup::timeValid();      // is the clock usable, however set
    char iso[32];
    if (bringup::timeIso8601(iso, sizeof(iso))) {
        t["utc"] = iso;
    }

    // Your project's fields, on the same object.
    appBuildStatus(out);
}

void handleApiStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    buildStatus(doc.to<JsonObject>());

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}
