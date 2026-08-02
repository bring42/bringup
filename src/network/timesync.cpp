#include "timesync.h"
#include "../constants.h"
#include "../logging.h"

#if BRINGUP_TIME_SYNC
#include <esp_sntp.h>
#endif

namespace bringup {

namespace {

// Any clock past this is real; anything below it is the 1970 power-on value or
// a nonsense partial set. Deliberately a fixed date in the past (2023-11-14)
// rather than something derived from the build, so a device that sits on a
// shelf for two years still evaluates it correctly.
constexpr time_t kMinValidEpoch = 1700000000;

#if BRINGUP_TIME_SYNC
bool g_started = false;

// Fired by SNTP each time it lands a sample. Worth logging the FIRST one: a
// device that silently never gets time is the failure mode that makes
// certificate verification look broken for reasons that point nowhere near the
// clock.
void onTimeSynced(struct timeval* tv) {
    static bool first = true;
    if (!first) return;
    first = false;
    char iso[32];
    timeIso8601(iso, sizeof(iso));
    LOG_INFO(LogTag::MAIN, "Time synced: %s (epoch %lld)",
             iso[0] ? iso : "?", (long long)(tv ? tv->tv_sec : 0));
}

// Empty macro -> nullptr, so lwip leaves the slot unused instead of trying to
// resolve "". That is what lets a DHCP-supplied server occupy it.
const char* orNull(const char* s) { return (s && s[0]) ? s : nullptr; }
#endif

} // namespace

void timeSyncBegin() {
#if BRINGUP_TIME_SYNC
    if (g_started) return;

    // MUST precede configTzTime(): that calls sntp_init() internally, and the
    // DHCP server-mode flag is only consulted while SNTP starts up.
    esp_sntp_servermode_dhcp(true);
    sntp_set_time_sync_notification_cb(onTimeSynced);

    configTzTime(BRINGUP_TZ,
                 orNull(BRINGUP_NTP_SERVER1),
                 orNull(BRINGUP_NTP_SERVER2),
                 orNull(BRINGUP_NTP_SERVER3));

    const bool explicitServers = orNull(BRINGUP_NTP_SERVER1) ||
                                 orNull(BRINGUP_NTP_SERVER2) ||
                                 orNull(BRINGUP_NTP_SERVER3);
    LOG_INFO(LogTag::MAIN, "Time sync started (%s, TZ=%s)",
             explicitServers ? "configured servers + DHCP" : "DHCP-supplied only",
             BRINGUP_TZ);
    if (!explicitServers) {
        LOG_DEBUG(LogTag::MAIN,
                  "No NTP server configured — relying on DHCP option 42. If your "
                  "router doesn't advertise one, set BRINGUP_NTP_SERVER1.");
    }
    g_started = true;
#endif
}

bool timeValid() {
    // No #if: the clock's validity is independent of whether WE set it. See the
    // header — the RTC survives a reflash, and apps may set time another way.
    return time(nullptr) > kMinValidEpoch;
}

time_t timeNow() {
    time_t now = time(nullptr);
    return now > kMinValidEpoch ? now : 0;
}

size_t timeIso8601(char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    time_t now = timeNow();
    if (now == 0) return 0;
    struct tm utc;
    gmtime_r(&now, &utc);
    return strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &utc);
}

} // namespace bringup
