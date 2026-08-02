/**
 * firmware.cpp - HTTP surface for the pull-based OTA updater.
 *
 * The heavy lifting (HTTPS download, SHA-256 verify, Update.h flashing) lives in
 * src/network/updater.*; these handlers only trigger the worker and report its
 * state, so they return promptly and never block the async web server.
 */

#include "firmware.h"
#include "../constants.h"
#include "../network/updater.h"
#include <ArduinoJson.h>

extern bool checkAuth(AsyncWebServerRequest* request);
extern void sendUnauthorized(AsyncWebServerRequest* request);

static void serializeStatus(const bringup::UpdateStatus& s, String& out) {
    JsonDocument doc;
    doc["phase"]           = bringup::updatePhaseName(s.phase);
    doc["current"]         = s.current;
    doc["fsVersion"]       = s.fsVersion[0] ? s.fsVersion : (const char*)nullptr;
    doc["latest"]          = s.latest[0] ? s.latest : (const char*)nullptr;
    doc["updateAvailable"] = s.updateAvailable;   // app OR fs behind
    doc["appAvailable"]    = s.appAvailable;
    doc["fsAvailable"]     = s.fsAvailable;
    doc["notes"]           = s.notes[0] ? s.notes : (const char*)nullptr;
    doc["percent"]         = s.percent;
    if (s.stage[0]) doc["stage"] = s.stage;       // "app" | "fs" while in progress
    if (s.error[0]) doc["error"] = s.error;
    serializeJson(doc, out);
}

void handleApiFirmwareCheck(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) { sendUnauthorized(request); return; }
    if (!bringup::requestUpdateCheck()) {
        request->send(409, "application/json",
                      "{\"error\":\"Updater busy\"}");
        return;
    }
    request->send(202, "application/json", "{\"status\":\"checking\"}");
}

void handleApiFirmwareStatus(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) { sendUnauthorized(request); return; }
    String out;
    serializeStatus(bringup::updaterStatus(), out);
    request->send(200, "application/json", out);
}

void handleApiFirmwareUpdate(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) { sendUnauthorized(request); return; }
    bringup::UpdateStatus s = bringup::updaterStatus();
    if (!s.updateAvailable) {
        request->send(400, "application/json",
                      "{\"error\":\"No update available; run /api/firmware/check first\"}");
        return;
    }
    if (!bringup::requestUpdate()) {
        request->send(409, "application/json", "{\"error\":\"Updater busy\"}");
        return;
    }
    request->send(202, "application/json", "{\"status\":\"updating\",\"target\":\"both\"}");
}

void handleApiFirmwareUpdateApp(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) { sendUnauthorized(request); return; }
    bringup::UpdateStatus s = bringup::updaterStatus();
    if (!s.appAvailable) {
        request->send(400, "application/json",
                      "{\"error\":\"No firmware update available; run /api/firmware/check first\"}");
        return;
    }
    if (!bringup::requestAppUpdate()) {
        request->send(409, "application/json", "{\"error\":\"Updater busy\"}");
        return;
    }
    request->send(202, "application/json", "{\"status\":\"updating\",\"target\":\"app\"}");
}

void handleApiFirmwareUpdateFs(AsyncWebServerRequest* request) {
    if (!checkAuth(request)) { sendUnauthorized(request); return; }
    bringup::UpdateStatus s = bringup::updaterStatus();
    if (!s.fsAvailable) {
        request->send(400, "application/json",
                      "{\"error\":\"No filesystem update available; run /api/firmware/check first\"}");
        return;
    }
    if (!bringup::requestFsUpdate()) {
        request->send(409, "application/json", "{\"error\":\"Updater busy\"}");
        return;
    }
    request->send(202, "application/json", "{\"status\":\"updating\",\"target\":\"fs\"}");
}
