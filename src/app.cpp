/**
 * app.cpp — YOUR PROJECT GOES HERE.
 *
 * The example below is a blinking LED with a web endpoint to control it. It
 * exists to show the shape of each hook; delete it and write your own.
 *
 * Everything the base gives you is already running by the time appSetup() is
 * called: WiFi provisioning over the setup AP, the web UI on port 80, NVS
 * config, mDNS, push OTA, and self-update from GitHub Releases.
 */

#include "app.h"
#include "constants.h"
#include "logging.h"
#include "main.h"        // checkAuth / sendUnauthorized / beginBody / endBody
#include "storage.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>

// Not every board variant defines LED_BUILTIN (the C3 devkits are inconsistent
// about it). Fall back to a safe non-strapping pin rather than failing to build.
#ifndef LED_BUILTIN
#define LED_BUILTIN 8
#endif

// ── Example state ───────────────────────────────────────────────────────────
// Owned by the loop task. The web handler below never writes it directly — it
// posts an intent that appLoop() consumes, which is the pattern that keeps a
// single writer for anything the loop reads every pass.
namespace {

constexpr uint8_t kStatusLedPin = LED_BUILTIN;

bool     g_blinking   = true;
uint32_t g_periodMs   = 1000;
bool     g_ledOn      = false;
uint32_t g_lastToggle = 0;

// Web -> loop handoff. Word-sized and only ever written from the web task /
// read-and-cleared from the loop task, so no lock is needed. A queue is the
// right answer once you have more than a couple of these.
std::atomic<bool>     g_pendingChange{false};
std::atomic<bool>     g_pendingBlinking{true};
std::atomic<uint32_t> g_pendingPeriodMs{1000};

}  // namespace

// ── Hooks ───────────────────────────────────────────────────────────────────

void appSetup() {
    pinMode(kStatusLedPin, OUTPUT);
    digitalWrite(kStatusLedPin, LOW);
    LOG_INFO(LogTag::APP, "Example blinker ready (pin %d)", kStatusLedPin);
}

void appLoop() {
    // Adopt any change requested by the web task.
    if (g_pendingChange.exchange(false)) {
        g_blinking = g_pendingBlinking.load();
        g_periodMs = g_pendingPeriodMs.load();
        LOG_INFO(LogTag::APP, "Blink: %s, period %u ms",
                 g_blinking ? "on" : "off", (unsigned)g_periodMs);
    }

    if (!g_blinking) {
        if (g_ledOn) { g_ledOn = false; digitalWrite(kStatusLedPin, LOW); }
        return;
    }

    // millis() subtraction, not comparison — correct across the ~49-day wrap.
    if (millis() - g_lastToggle >= g_periodMs / 2) {
        g_lastToggle = millis();
        g_ledOn = !g_ledOn;
        digitalWrite(kStatusLedPin, g_ledOn ? HIGH : LOW);
    }
}

void appRegisterRoutes(AsyncWebServer& server) {
    // GET /api/blink -> current state
    server.on("/api/blink", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["blinking"] = g_pendingBlinking.load();
        doc["periodMs"] = g_pendingPeriodMs.load();
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // POST /api/blink {"blinking":true,"periodMs":500}
    //
    // Note the shape: auth first, then body assembly through the BodyGuard
    // (AsyncTCP interleaves chunks of concurrent requests, so a bare `static
    // String` accumulator would let two POSTs corrupt each other), then a
    // length-aware append, then publish an intent and return. No hardware is
    // touched on this task.
    server.on("/api/blink", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
           size_t index, size_t total) {
            static String body;

            if (index == 0) {
                if (!checkAuth(request)) { sendUnauthorized(request); return; }
                if (!beginBody(request)) {
                    request->send(409, "application/json", "{\"error\":\"Busy, retry\"}");
                    return;
                }
                body = "";
                if (total > MAX_REQUEST_BODY_SIZE) {
                    endBody(request);
                    request->send(413, "application/json", "{\"error\":\"Body too large\"}");
                    return;
                }
            }

            // Length-aware: the chunk is NOT NUL-terminated, so String((char*)data)
            // would strlen past `len` into adjacent memory.
            body += String((char*)data, len);

            if (index + len >= total) {
                endBody(request);
                JsonDocument doc;
                if (deserializeJson(doc, body)) {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    return;
                }
                if (doc["blinking"].is<bool>()) {
                    g_pendingBlinking.store(doc["blinking"].as<bool>());
                }
                if (doc["periodMs"].is<int>()) {
                    g_pendingPeriodMs.store(
                        constrain(doc["periodMs"].as<int>(), 50, 60000));
                }
                g_pendingChange.store(true);   // last: publishes the values above
                request->send(200, "application/json", "{\"success\":true}");
            }
        });
}

void appBuildStatus(JsonObject status) {
    // Reads only atomics — safe from either task. Anything heavier belongs
    // behind a snapshot, like updaterStatus() does.
    status["blinking"] = g_pendingBlinking.load();
    status["periodMs"] = g_pendingPeriodMs.load();
}

void appOnNetworkUp() {
    // MQTT connect, NTP sync, UDP listener… Runs on every reconnect too, so
    // whatever you do here must be safe to repeat.
}

void appOnNetworkDown() {
    // Stop what appOnNetworkUp() started.
}

void appOnUpdateStarting() {
    // About to rewrite flash and reboot. Park everything.
    g_blinking = false;
    digitalWrite(kStatusLedPin, LOW);
}
