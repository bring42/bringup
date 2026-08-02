#pragma once

#include <Arduino.h>
#include <time.h>

// Wall-clock time via SNTP. OFF unless BRINGUP_TIME_SYNC is 1 — see constants.h.
//
// The base itself needs no clock: everything internal is millis()-based, which
// is monotonic and needs no network. Time sync is here because two things want
// it, and both are opt-in:
//
//   • timestamps, scheduling, "last seen" — ordinary app reasons
//   • TLS certificate verification, which CANNOT work without a correct clock:
//     an ESP32 boots at 1970, so every certificate reads as "not yet valid" and
//     every handshake fails, with an error that points nowhere near the clock
//
// Default server policy is deliberately "whatever your router says". The
// Arduino core ships CONFIG_LWIP_DHCP_GET_NTP_SRV=y, so if your DHCP server
// advertises option 42 the device gets time without contacting anyone you
// didn't already trust, and without you choosing a provider. Set an explicit
// server only if your router doesn't advertise one.

namespace bringup {

// Start (or restart) SNTP. Idempotent — safe to call on every reconnect.
// No-op when BRINGUP_TIME_SYNC is 0.
void timeSyncBegin();

// Is the wall clock plausibly real, rather than the 1970 power-on value?
//
// Deliberately NOT "is SNTP running". The clock can be valid without this
// module: the ESP32's RTC keeps running across a reflash (so a value survives
// from a previous build), and an app may set it from a GPS or an RTC chip. What
// callers actually need to know — TLS certificate validation above all — is
// whether the clock can be trusted, not how it got set.
bool timeValid();

// Seconds since epoch, or 0 if the clock has never been set.
time_t timeNow();

// "2026-08-02T11:36:28Z" into `out`, or "" if the clock is not valid. Returns
// bytes written. Always UTC regardless of BRINGUP_TZ — this is for logs and
// comparisons, where a local-time string is a liability.
size_t timeIso8601(char* out, size_t cap);

} // namespace bringup
