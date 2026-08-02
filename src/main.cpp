/**
 * Bringup — a ground template for ESP32 projects that want a web UI and
 * self-updating firmware.
 *
 * What you get before writing a line of project code:
 *   • WiFi in permanent AP+STA mode — provision over the "Bringup-Setup" AP,
 *     and stay reachable there forever if the saved network disappears.
 *   • An async web server on port 80 serving a gzipped UI from LittleFS, with
 *     content-hashed asset URLs so a browser can never pin a stale build.
 *   • Self-update from GitHub Releases: the device fetches manifest.json,
 *     compares the published version to its own firmware AND filesystem, and
 *     flashes verified images over HTTPS into the inactive OTA slot.
 *   • Push OTA + mDNS for development, NVS-backed config, optional API token,
 *     a /health endpoint, and a watchdog.
 *
 * YOUR CODE GOES IN src/app.cpp. This file should rarely need editing.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>      // esp_ota_mark_app_valid_cancel_rollback

#include "main.h"
#include "app.h"
#include "constants.h"
#include "logging.h"
#include "storage.h"
#include "core/body_guard.h"

#include "network/server.h"   // web server routes
#include "network/ota.h"      // push OTA + mDNS (dev-time)
#include "network/updater.h"  // pull OTA from GitHub Releases (deployed)
#include "network/wifi.h"     // AP+STA setup and maintenance

// Optional development secrets (copy src/secrets.h.example -> src/secrets.h,
// which is gitignored). Lets you flash a dev build that joins your WiFi without
// going through the setup AP every time.
#ifdef __has_include
#  if __has_include("secrets.h")
#    include "secrets.h"
#    define HAS_SECRETS_H
#  endif
#endif

// ═══════════════════════════════════════════════════════════════════════════
// Globals (declared in main.h)
// ═══════════════════════════════════════════════════════════════════════════

AsyncWebServer server(80);
Config config;
bool wifiConnected = false;
bool webUiAvailable = false;
unsigned long lastWifiAttempt = 0;

// ═══════════════════════════════════════════════════════════════════════════
// Auth + body assembly
// ═══════════════════════════════════════════════════════════════════════════

static bringup::BodyGuard g_bodyGuard;

bool beginBody(AsyncWebServerRequest* request) {
    return g_bodyGuard.begin(request, millis());
}

void endBody(AsyncWebServerRequest* request) {
    g_bodyGuard.end(request);
}

bool checkAuth(AsyncWebServerRequest* request) {
    // No token configured -> everything is open. This is what makes first-boot
    // provisioning possible at all; set a token from the UI once you're online.
    if (config.authToken.length() == 0) {
        return true;
    }

    if (request->hasHeader("Authorization")) {
        String authHeader = request->header("Authorization");
        if (authHeader.startsWith("Bearer ")) {
            authHeader = authHeader.substring(7);
        }
        if (authHeader == config.authToken) {
            return true;
        }
    }

    if (request->hasHeader("X-API-Key")) {
        if (request->header("X-API-Key") == config.authToken) {
            return true;
        }
    }

    // Query parameter, for browser testing.
    if (request->hasParam("token")) {
        if (request->getParam("token")->value() == config.authToken) {
            return true;
        }
    }

    return false;
}

void sendUnauthorized(AsyncWebServerRequest* request) {
    request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
}

// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(1000);
    LOG_INFO(LogTag::MAIN, "=== %s v%s (%s) ===",
             FIRMWARE_NAME, FIRMWARE_VERSION, FIRMWARE_BUILD_HASH);

    storage.begin();

    // Mount LittleFS for the web UI. format-on-fail (true) means a device with
    // no filesystem image yet comes up serving nothing rather than failing to
    // boot — you can still reach the API and push a filesystem over OTA.
    if (LittleFS.begin(true)) {
        webUiAvailable = true;
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        LOG_INFO(LogTag::WEB, "LittleFS mounted (%u KB used of %u KB)",
                 (unsigned)(used / 1024), (unsigned)(total / 1024));
    } else {
        LOG_ERROR(LogTag::WEB, "Failed to mount LittleFS; web UI unavailable");
    }

    if (!storage.loadConfig(config)) {
        LOG_WARN(LogTag::STORAGE, "No config found, using defaults");
    } else {
        LOG_INFO(LogTag::STORAGE, "Config loaded (SSID: %s)",
                 config.wifiSSID.length() ? config.wifiSSID.c_str() : "(unset)");
    }

#ifdef HAS_SECRETS_H
    LOG_DEBUG(LogTag::MAIN, "Applying development secrets");
    if (String(DEV_WIFI_SSID).length() > 0) {
        config.wifiSSID = DEV_WIFI_SSID;
        config.wifiPassword = DEV_WIFI_PASSWORD;
    }
#endif

    // WiFi first (non-blocking), then the server: the AP and the UI must be up
    // immediately, even if the saved network is unreachable.
    setupWiFi();
    setupServer();

    // Start the self-update worker. Its HTTPS download would block the AsyncTCP
    // task, so it lives on its own task and the UI polls it for status.
    bringup::initUpdater();

    appSetup();

    // If we just booted a freshly-OTA'd image and the bootloader supports
    // rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE), confirm the image so the
    // bootloader stops treating it as pending. No-op on the default Arduino
    // sdkconfig (rollback disabled) — harmless, and correct once it's enabled.
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        LOG_INFO(LogTag::OTA, "Running image marked valid (rollback confirmed)");
    }

    LOG_INFO(LogTag::MAIN, "Setup complete");
    logMemoryStats(LogTag::MAIN, "at startup");

    esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);   // panic + reboot on timeout
    esp_task_wdt_add(NULL);                          // watch the loop task
    LOG_INFO(LogTag::MAIN, "Watchdog armed (%ds)", WATCHDOG_TIMEOUT_SEC);
}

void loop() {
    ArduinoOTA.handle();        // push OTA (dev-time)
    handleWifiMaintenance();    // reconnect, connect edges, diagnostics
    loopServer();               // WebSocket cleanup / periodic broadcast

    appLoop();                  // ← your project

    esp_task_wdt_reset();       // proves the loop is still running

    // Periodic vitals at DEBUG level. Heap trend catches a slow leak; the die
    // temperature catches a core pinned at 100% or a peripheral left clocked.
    static uint32_t lastVitals = 0;
    if (millis() - lastVitals >= 30000) {
        lastVitals = millis();
        LOG_DEBUG(LogTag::MAIN, "vitals: heap %u (min %u, block %u)  temp %.1fC  up %us",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                  temperatureRead(), (unsigned)(millis() / 1000));
    }

    // delay(1), NOT yield(). Every task above is millis()-gated, so spinning at
    // full speed only re-asks "is it time yet?" ~100k times a second, and
    // yield() never blocks — so the FreeRTOS idle task on this core never runs.
    // 1 ms still gives a ~1 kHz loop, far faster than anything here needs (the
    // WebSocket broadcasts at 1 Hz, WiFi maintenance every 30 s). Work needing
    // tighter timing than 1 ms belongs on its own task at a higher priority,
    // not in a hot loop() that starves the idle task.
    //
    // NOTE: this is good practice, NOT a thermal fix. Measured on a XIAO S3,
    // yield() and delay(1) converge to the same ~62 C die temperature; the
    // clock rate is what moves that number. See docs/POWER.md.
    delay(1);
}
