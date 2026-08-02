#ifndef BRINGUP_SEMVER_H
#define BRINGUP_SEMVER_H

#include <string.h>
#include <stdlib.h>

namespace bringup {

// Is `candidate` strictly newer than `running`?
//
// Compares the x.y.z triple only; any -prerelease or +build suffix is ignored,
// and a non-numeric component parses as 0. That means "1.2.0-rc1" and "1.2.0"
// compare EQUAL — a pre-release tag will not be offered as an update to a
// device already on the release, which is the safe direction.
//
// Pure and dependency-free (no Arduino String) so it is unit-testable on the
// host: this is the function that decides whether a device flashes itself, and
// an off-by-one here is a fleet-wide update loop or a fleet that never updates.
// See test/test_semver/.
inline void parseSemver(const char* v, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    if (!v) return;

    int idx = 0;
    const char* p = v;
    while (*p == 'v' || *p == 'V') p++;   // tolerate a leading "v"

    while (idx < 3) {
        // strtol stops at the first non-digit, so a garbage component yields 0.
        char* end = nullptr;
        long value = strtol(p, &end, 10);
        if (end == p) {          // no digits at all -> component is 0
            value = 0;
        }
        out[idx++] = (int)value;

        if (end && *end == '.') {
            p = end + 1;         // next component
            continue;
        }
        break;                   // '-', '+', '\0' or junk: stop, rest stays 0
    }
}

inline bool isNewer(const char* candidate, const char* running) {
    int a[3], b[3];
    parseSemver(candidate, a);
    parseSemver(running, b);
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

} // namespace bringup

#endif // BRINGUP_SEMVER_H
