#ifndef BRINGUP_CONSTANTS_H
#define BRINGUP_CONSTANTS_H

#include <cstdint>
#include <cstddef>

// ═══════════════════════════════════════════════════════════════════════════
// PROJECT IDENTITY
// ═══════════════════════════════════════════════════════════════════════════

#define FIRMWARE_NAME    "Bringup"

// mDNS hostname — the device is reachable at http://bringup.local once it
// joins your network. Also used as the ArduinoOTA hostname.
#define MDNS_HOSTNAME    "bringup"

// Setup access point, always on (AP+STA). This is how you provision WiFi and
// how you recover a device whose saved network is gone.
#define AP_SSID          "Bringup-Setup"
#define AP_PASSWORD      "bringup123"      // ⚠️ min 8 chars; change per project

// ═══════════════════════════════════════════════════════════════════════════
// NETWORK
// ═══════════════════════════════════════════════════════════════════════════

// STA reconnect cadence. Reconnect is SKIPPED entirely while a client is parked
// on the SoftAP (see handleWifiMaintenance): the single-radio scan a reconnect
// triggers channel-hops the AP off the air and breaks the provisioning client's
// DHCP handshake mid-setup.
constexpr uint32_t WIFI_RETRY_INTERVAL_MS   = 30000;

// Diagnostic scan cadence while disconnected. Deliberately not a multiple of
// the retry interval, so a scan and a WiFi.begin() rarely collide.
constexpr uint32_t WIFI_DIAG_SCAN_INTERVAL_MS = 95000;

// ═══════════════════════════════════════════════════════════════════════════
// SYSTEM LIMITS & BUFFERS
// ═══════════════════════════════════════════════════════════════════════════

constexpr size_t MAX_REQUEST_BODY_SIZE      = 16384;  // 16 KB max POST body

// Firmware updater worker (pull-based OTA from GitHub Releases). A generous
// stack: the worker runs a TLS download + streaming SHA-256 + Update.write.
constexpr size_t   UPDATER_TASK_STACK_SIZE   = 16384;
constexpr uint8_t  UPDATER_TASK_PRIORITY     = 1;
constexpr uint8_t  UPDATER_TASK_CORE         = 0;
// Chunk size for streaming the downloaded image into Update.write + the hasher.
constexpr size_t   UPDATER_CHUNK_SIZE        = 2048;
// How long the updater waits on a stalled HTTP read before aborting a download.
constexpr uint32_t UPDATER_HTTP_TIMEOUT_MS   = 20000;

// Watchdog. loop() must call esp_task_wdt_reset() more often than this; if your
// app does long blocking work on the loop task, move it to its own task rather
// than raising this.
constexpr uint32_t WATCHDOG_TIMEOUT_SEC     = 30;

// ═══════════════════════════════════════════════════════════════════════════
// FIRMWARE METADATA
// ═══════════════════════════════════════════════════════════════════════════

// FIRMWARE_VERSION is normally injected at build time from the git tag by
// scripts/version.py (e.g. -DFIRMWARE_VERSION=\"1.2.0\"). This fallback is what
// a plain local `pio run` (no tag) compiles with. A device running the fallback
// will consider ANY published release newer than itself.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.1.0"
#endif

// Short git hash, injected by scripts/version.py; "dev" for un-tagged builds.
#ifndef FIRMWARE_BUILD_HASH
#define FIRMWARE_BUILD_HASH "dev"
#endif

#ifndef FIRMWARE_BUILD_TIMESTAMP
#define FIRMWARE_BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif

// ═══════════════════════════════════════════════════════════════════════════
// FIRMWARE AUTO-UPDATE (pull-based OTA from GitHub Releases)
// ═══════════════════════════════════════════════════════════════════════════

// ⚠️ SET THESE. The device builds its manifest URL from them:
//   https://github.com/<OWNER>/<REPO>/releases/latest/download/manifest.json
// Until they point at a real repo with a published release, "Check for update"
// will fail with an HTTP error — that is the expected behavior, not a bug.
#ifndef BRINGUP_GH_OWNER
#define BRINGUP_GH_OWNER "bring42"
#endif
#ifndef BRINGUP_GH_REPO
#define BRINGUP_GH_REPO "bringup"
#endif

// Which release asset set belongs to THIS build. Injected per-env by
// scripts/version.py from boards.json. Must match the keys CI writes into
// manifest.json ("boards": { <id>: ... }) — a mismatch means the device
// downloads a manifest it cannot find itself in, and reports exactly that.
#ifndef BRINGUP_BOARD_ID
#define BRINGUP_BOARD_ID "esp32-s3-devkitc"
#endif

#endif // BRINGUP_CONSTANTS_H
