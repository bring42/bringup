/**
 * Host tests for the version comparison that decides whether a deployed device
 * flashes itself. Getting this wrong is expensive in both directions: too eager
 * and the fleet reboot-loops onto the same build; too shy and no device ever
 * updates again.
 */
#include <unity.h>
#include "network/semver.h"

using bringup::isNewer;

void test_strictly_newer(void) {
    TEST_ASSERT_TRUE(isNewer("1.0.1", "1.0.0"));
    TEST_ASSERT_TRUE(isNewer("1.1.0", "1.0.9"));
    TEST_ASSERT_TRUE(isNewer("2.0.0", "1.99.99"));
}

void test_not_newer(void) {
    TEST_ASSERT_FALSE(isNewer("1.0.0", "1.0.0"));   // equal is NOT newer
    TEST_ASSERT_FALSE(isNewer("1.0.0", "1.0.1"));
    TEST_ASSERT_FALSE(isNewer("1.0.9", "1.1.0"));
    TEST_ASSERT_FALSE(isNewer("1.99.99", "2.0.0"));
}

void test_component_order(void) {
    // Numeric, not lexicographic: "10" > "9" even though "1" < "9" as text.
    TEST_ASSERT_TRUE(isNewer("1.10.0", "1.9.0"));
    TEST_ASSERT_TRUE(isNewer("10.0.0", "9.0.0"));
    TEST_ASSERT_FALSE(isNewer("1.9.0", "1.10.0"));
}

void test_leading_v_tolerated(void) {
    // Release tags are "v1.2.0"; version.py strips the v, but the device may
    // read a manifest written by hand.
    TEST_ASSERT_TRUE(isNewer("v1.0.1", "1.0.0"));
    TEST_ASSERT_TRUE(isNewer("1.0.1", "v1.0.0"));
    TEST_ASSERT_FALSE(isNewer("v1.0.0", "v1.0.0"));
}

void test_suffixes_ignored(void) {
    // A pre-release compares EQUAL to its release, so a device on 1.2.0 is
    // never offered "1.2.0-rc1" as an update. Safe direction.
    TEST_ASSERT_FALSE(isNewer("1.2.0-rc1", "1.2.0"));
    TEST_ASSERT_FALSE(isNewer("1.2.0", "1.2.0-rc1"));
    TEST_ASSERT_TRUE(isNewer("1.2.1-rc1", "1.2.0"));
    TEST_ASSERT_TRUE(isNewer("1.3.0+build7", "1.2.9"));
}

void test_short_and_malformed(void) {
    // Missing components read as 0.
    TEST_ASSERT_TRUE(isNewer("1.1", "1.0.5"));
    TEST_ASSERT_FALSE(isNewer("1.0", "1.0.0"));
    TEST_ASSERT_TRUE(isNewer("2", "1.9.9"));

    // Garbage parses as 0.0.0 and therefore never wins. An unparseable manifest
    // version must not trigger a flash.
    TEST_ASSERT_FALSE(isNewer("", "1.0.0"));
    TEST_ASSERT_FALSE(isNewer("banana", "1.0.0"));
    TEST_ASSERT_FALSE(isNewer(nullptr, "1.0.0"));

    // ...but a real version IS newer than garbage or nothing, which is what
    // makes an unstamped filesystem self-heal.
    TEST_ASSERT_TRUE(isNewer("1.0.0", ""));
    TEST_ASSERT_TRUE(isNewer("1.0.0", nullptr));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_strictly_newer);
    RUN_TEST(test_not_newer);
    RUN_TEST(test_component_order);
    RUN_TEST(test_leading_v_tolerated);
    RUN_TEST(test_suffixes_ignored);
    RUN_TEST(test_short_and_malformed);
    return UNITY_END();
}
