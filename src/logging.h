#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================
// Structured Logging System
// ============================================
//
// Every log line is assembled in full and emitted with ONE Serial.write, under
// a mutex. That matters more than it sounds: several tasks log concurrently
// here (the loop task, the AsyncTCP task, the OTA worker), and a line built
// from several Serial calls gets chopped apart by whichever task preempts
// mid-line. Observed during a real OTA run:
//
//     [61815] [I] [OTA ] Updater: appl
//     [61816] [I[OTA ]
//
// which corrupts precisely the logs you are reading when something goes wrong
// during an update.
//
// NOT interrupt-safe: do not call these from an ISR (it would try to take a
// mutex). Use ets_printf there instead.

// Log levels (can be filtered at compile time)
enum class LogLevel : uint8_t {
    DEBUG = 0,   // Verbose debugging info
    INFO = 1,    // Normal operational messages
    WARN = 2,    // Warning conditions
    ERROR = 3,   // Error conditions
    NONE = 4     // Disable all logging
};

// Set minimum log level (compile-time filter)
// Change this to filter out lower-priority logs
#ifndef LOG_LEVEL
#define LOG_LEVEL LogLevel::DEBUG
#endif

// Component tags for filtering/identification
// Add your own here; the format reserves 4 columns, so keep them short.
namespace LogTag {
    constexpr const char* MAIN    = "MAIN";
    constexpr const char* WIFI    = "WIFI";
    constexpr const char* WEB     = "WEB";
    constexpr const char* OTA     = "OTA";
    constexpr const char* STORAGE = "NVS";
    constexpr const char* APP     = "APP";
}

// Internal logging implementation
class Logger {
public:
    static void log(LogLevel level, const char* tag, const char* format, ...) {
        if (level < LOG_LEVEL) return;

        char body[256];
        va_list args;
        va_start(args, format);
        vsnprintf(body, sizeof(body), format, args);
        va_end(args);

        // Whole line, one buffer, one write.
        char line[320];
        int n = snprintf(line, sizeof(line), "[%8lu] [%c] [%-4s] %s\r\n",
                         (unsigned long)millis(), levelChar(level), tag, body);
        if (n <= 0) return;
        if (n > (int)sizeof(line) - 1) n = sizeof(line) - 1;   // snprintf truncated

        Lock guard;
        writeAll(line, n);
    }

    // Log with hex dump (useful for debugging binary data). Holds the lock for
    // the whole dump so the rows can't be split by another task's line — the
    // mutex is recursive, so the log() call below can re-take it safely.
    static void logHex(LogLevel level, const char* tag, const char* label,
                       const uint8_t* data, size_t len, size_t maxLen = 32) {
        if (level < LOG_LEVEL) return;

        Lock guard;
        log(level, tag, "%s (%u bytes):", label, (unsigned)len);

        size_t printLen = len < maxLen ? len : maxLen;
        Serial.print("    ");
        for (size_t i = 0; i < printLen; i++) {
            Serial.printf("%02X ", data[i]);
            if ((i + 1) % 16 == 0 && i + 1 < printLen) {
                Serial.print("\r\n    ");
            }
        }
        if (len > maxLen) {
            Serial.printf("... (%u more bytes)", (unsigned)(len - maxLen));
        }
        Serial.println();
    }

private:
    // Serial.write() CAN RETURN SHORT. On the USB-serial-JTAG path, HWCDC::write
    // gives up when its ring buffer stays full ("write failed due to ring buffer
    // full - timeout") and returns however many bytes it managed. Ignoring that
    // return silently truncates the line — and during a burst the next line
    // lands in the gap, so you lose a whole message and corrupt its neighbour.
    // Seen during an OTA: the "applying update" line vanished entirely and its
    // tail was spliced onto the end of the previous one.
    //
    // Retry while we're making progress; give up after a few consecutive
    // no-progress attempts. A logger must never stall the system it reports on,
    // so losing a line is the correct failure — but only after trying.
    static void writeAll(const char* buf, int len) {
        int off = 0;
        int stalls = 0;
        while (off < len && stalls < 3) {
            size_t w = Serial.write(reinterpret_cast<const uint8_t*>(buf) + off,
                                    (size_t)(len - off));
            if (w == 0) {
                stalls++;          // no progress (host not draining / CDC gone)
            } else {
                off += (int)w;
                stalls = 0;        // progress resets the budget
            }
        }
    }

    static char levelChar(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return 'D';
            case LogLevel::INFO:  return 'I';
            case LogLevel::WARN:  return 'W';
            case LogLevel::ERROR: return 'E';
            default:              return '?';
        }
    }

    // Recursive so logHex can hold it across its own log() call. Created on
    // first use; the function-local static makes that thread-safe.
    static SemaphoreHandle_t mutex() {
        static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
        return m;
    }

    // RAII, with a timeout rather than portMAX_DELAY: a logger must never be
    // able to wedge the system it is reporting on. If the lock can't be had in
    // time we print anyway — a garbled line beats a lost one, and beats a hang.
    struct Lock {
        bool held;
        Lock() {
            SemaphoreHandle_t m = mutex();
            held = m && xSemaphoreTakeRecursive(m, pdMS_TO_TICKS(50)) == pdTRUE;
        }
        ~Lock() { if (held) xSemaphoreGiveRecursive(mutex()); }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    };
};

// Convenience macros for logging
#define LOG_DEBUG(tag, fmt, ...) Logger::log(LogLevel::DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  Logger::log(LogLevel::INFO, tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  Logger::log(LogLevel::WARN, tag, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) Logger::log(LogLevel::ERROR, tag, fmt, ##__VA_ARGS__)

// Hex dump macro
#define LOG_HEX(level, tag, label, data, len) Logger::logHex(level, tag, label, data, len)

// Memory stats helper
inline void logMemoryStats(const char* tag, const char* context = "") {
    LOG_DEBUG(tag, "Heap free: %u, largest block: %u %s",
              ESP.getFreeHeap(), ESP.getMaxAllocHeap(), context);
}

#endif // LOGGING_H
