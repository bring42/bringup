#include "storage.h"

const char* Storage::NAMESPACE_CONFIG = "config";
const char* Storage::NAMESPACE_APP    = "appstate";

Storage storage;

Storage::Storage() {
    mutex_ = xSemaphoreCreateRecursiveMutex();
}

bool Storage::begin() {
    StorageLock lock(mutex_);
    return true;   // Preferences needs no explicit init
}

bool Storage::isReady() {
    StorageLock lock(mutex_);
    // Probe NVS by opening the config namespace read-WRITE: this succeeds whenever
    // NVS is healthy (creating the namespace if it doesn't exist yet) and fails only
    // when the partition is genuinely broken. A read-only open would false-negative
    // on a factory-fresh device whose config namespace no save has created yet.
    if (!prefs.begin(NAMESPACE_CONFIG, false)) {
        return false;
    }
    prefs.end();
    return true;
}

bool Storage::loadConfig(Config& config) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_CONFIG, true)) {   // read-only
        return false;
    }

    config.wifiSSID     = prefs.getString("ssid", "");
    config.wifiPassword = prefs.getString("pass", "");
    config.authToken    = prefs.getString("authtoken", "");
    config.deviceName   = prefs.getString("devname", FIRMWARE_NAME);

    prefs.end();
    return true;
}

bool Storage::saveConfig(const Config& config) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_CONFIG, false)) {  // read-write
        return false;
    }

    prefs.putString("ssid", config.wifiSSID);
    prefs.putString("pass", config.wifiPassword);
    prefs.putString("authtoken", config.authToken);
    prefs.putString("devname", config.deviceName);

    prefs.end();
    return true;
}

bool Storage::clearConfig() {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_CONFIG, false)) {
        return false;
    }
    prefs.clear();
    prefs.end();
    return true;
}

bool Storage::saveAppState(const JsonDocument& state) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_APP, false)) {
        return false;
    }

    String jsonStr;
    serializeJson(state, jsonStr);

    // NVS entries are limited; refuse rather than truncate into unparseable JSON.
    if (jsonStr.length() > 4000) {
        prefs.end();
        return false;
    }

    prefs.putString("state", jsonStr);
    prefs.end();
    return true;
}

bool Storage::loadAppState(JsonDocument& state) {
    StorageLock lock(mutex_);
    if (!prefs.begin(NAMESPACE_APP, true)) {
        return false;
    }

    String jsonStr = prefs.getString("state", "{}");
    prefs.end();

    return deserializeJson(state, jsonStr) == DeserializationError::Ok;
}

void Storage::configToJson(const Config& config, JsonDocument& doc, bool maskSecrets) {
    doc["wifiSSID"] = config.wifiSSID;
    // Never expose the WiFi password, masked or otherwise — there is no UI
    // reason to read it back, and "****" is enough to show one is set.
    doc["wifiPassword"] = "";
    doc["wifiPasswordSet"] = config.wifiPassword.length() > 0;
    doc["authToken"] = config.authToken.length() > 0 ? (maskSecrets ? "****" : config.authToken) : "";
    doc["authEnabled"] = config.authToken.length() > 0;
    doc["deviceName"] = config.deviceName;
}

bool Storage::configFromJson(Config& config, const JsonDocument& doc) {
    // Only update fields that are PRESENT. The spelling of each key is
    // load-bearing: ArduinoJson is case-sensitive, so a UI sending "wifiSsid"
    // instead of "wifiSSID" silently drops the SSID here with no error anywhere.
    // Keep data/assets/app.js matched to these exact keys.
    if (doc["wifiSSID"].is<const char*>()) {
        config.wifiSSID = doc["wifiSSID"].as<String>();
    }
    if (doc["wifiPassword"].is<const char*>()) {
        String pass = doc["wifiPassword"].as<String>();
        // Empty means "leave it alone" — the UI can't read the current one back,
        // so an empty field on a save must not wipe a working password.
        if (pass.length() > 0) {
            config.wifiPassword = pass;
        }
    }
    if (doc["authToken"].is<const char*>()) {
        String token = doc["authToken"].as<String>();
        // Don't overwrite with the mask we handed out; an explicit empty string
        // DOES clear the token (that's how you turn auth off).
        if (!token.startsWith("****")) {
            config.authToken = token;
        }
    }
    if (doc["deviceName"].is<const char*>()) {
        String name = doc["deviceName"].as<String>();
        if (name.length() > 0 && name.length() <= 31) {
            config.deviceName = name;
        }
    }

    return true;
}
