#ifndef BRINGUP_STORAGE_H
#define BRINGUP_STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "constants.h"   // FIRMWARE_NAME (default deviceName)

// RAII guard for Storage's NVS access. The single Preferences object is shared
// across tasks (a config save from the web task vs. whatever your app persists
// from the loop task) — concurrent begin()/end() corrupts the handle. The mutex
// is recursive so a locked method may safely call another locked method.
class StorageLock {
public:
    explicit StorageLock(SemaphoreHandle_t m) : m_(m) {
        if (m_) xSemaphoreTakeRecursive(m_, portMAX_DELAY);
    }
    ~StorageLock() { if (m_) xSemaphoreGiveRecursive(m_); }
    StorageLock(const StorageLock&) = delete;
    StorageLock& operator=(const StorageLock&) = delete;
private:
    SemaphoreHandle_t m_;
};

// Base device configuration, persisted in NVS.
//
// ADDING YOUR OWN FIELDS: add the member here, then load/save/toJson/fromJson
// in storage.cpp — all four, or the field silently won't round-trip. NVS keys
// are limited to 15 characters, which is why the existing ones are terse.
struct Config {
    String   wifiSSID;
    String   wifiPassword;
    String   authToken;       // empty = no auth required on the API
    String   deviceName;      // shown in the UI; does NOT change mDNS hostname

    Config()
        : wifiSSID(""),
          wifiPassword(""),
          authToken(""),
          deviceName(FIRMWARE_NAME) {}
};

class Storage {
public:
    Storage();

    bool begin();

    // Probe whether NVS is actually reachable. A real health signal for
    // /health — NVS is opened lazily, so begin() alone proves nothing.
    bool isReady();

    bool loadConfig(Config& config);
    bool saveConfig(const Config& config);
    bool clearConfig();

    // Free-form app state, stored as JSON under its own namespace. Convenient
    // for a handful of settings; NVS entries are capped (~4 KB here), so use
    // LittleFS for anything larger.
    bool saveAppState(const JsonDocument& state);
    bool loadAppState(JsonDocument& state);

    // Export config to JSON. Secrets are masked unless you explicitly ask for
    // them — the web UI always gets the masked form.
    void configToJson(const Config& config, JsonDocument& doc, bool maskSecrets = true);

    // Import config from JSON. Only fields PRESENT in the document are touched,
    // and masked placeholders ("****") are ignored, so a UI can POST back the
    // same object it was given without wiping the password it never saw.
    bool configFromJson(Config& config, const JsonDocument& doc);

private:
    Preferences prefs;
    SemaphoreHandle_t mutex_;   // guards all prefs access (see StorageLock)
    static const char* NAMESPACE_CONFIG;
    static const char* NAMESPACE_APP;
};

extern Storage storage;

#endif // BRINGUP_STORAGE_H
