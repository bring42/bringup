/**
 * Host tests for BodyGuard — the state machine that stops two concurrent POSTs
 * from corrupting each other's body accumulator on the single AsyncTCP task.
 */
#include <unity.h>
#include "core/body_guard.h"

using bringup::BodyGuard;

// Stand-ins for AsyncWebServerRequest*; only their addresses matter.
static int reqA, reqB;
static const void* A = &reqA;
static const void* B = &reqB;

void test_first_claim_granted(void) {
    BodyGuard g;
    TEST_ASSERT_TRUE(g.begin(A, 1000));
}

void test_reentry_across_chunks(void) {
    BodyGuard g;
    TEST_ASSERT_TRUE(g.begin(A, 1000));
    // Every chunk of the same request calls begin() again — all must be granted.
    TEST_ASSERT_TRUE(g.begin(A, 1005));
    TEST_ASSERT_TRUE(g.begin(A, 1010));
}

void test_second_requester_rejected(void) {
    BodyGuard g;
    TEST_ASSERT_TRUE(g.begin(A, 1000));
    TEST_ASSERT_FALSE(g.begin(B, 1001));   // -> handler sends 409
}

void test_slot_reusable_after_end(void) {
    BodyGuard g;
    TEST_ASSERT_TRUE(g.begin(A, 1000));
    g.end(A);
    TEST_ASSERT_TRUE(g.begin(B, 1001));
}

void test_end_by_non_owner_is_noop(void) {
    BodyGuard g;
    TEST_ASSERT_TRUE(g.begin(A, 1000));
    g.end(B);                              // B never owned it
    TEST_ASSERT_FALSE(g.begin(B, 1001));   // A's claim must still stand
}

void test_stale_claim_self_heals(void) {
    BodyGuard g;
    TEST_ASSERT_TRUE(g.begin(A, 1000));
    // A disconnected mid-body: AsyncTCP never delivers the last chunk, so end()
    // is never called. Without the timeout the slot would wedge forever and
    // every later POST would 409.
    TEST_ASSERT_FALSE(g.begin(B, 1000 + BodyGuard::kTimeoutMs - 1));
    TEST_ASSERT_TRUE(g.begin(B, 1000 + BodyGuard::kTimeoutMs));
}

void test_millis_wraparound(void) {
    BodyGuard g;
    // Claim just before the ~49-day millis() rollover.
    const uint32_t nearMax = 0xFFFFFFFFu - 100;
    TEST_ASSERT_TRUE(g.begin(A, nearMax));

    // 50 ms later, having wrapped through zero. Unsigned subtraction handles
    // this correctly; a naive (now > claimed + timeout) comparison would not,
    // and would free the slot out from under an in-flight request.
    TEST_ASSERT_FALSE(g.begin(B, nearMax + 50));

    // And the timeout still expires correctly across the wrap.
    TEST_ASSERT_TRUE(g.begin(B, nearMax + BodyGuard::kTimeoutMs));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_claim_granted);
    RUN_TEST(test_reentry_across_chunks);
    RUN_TEST(test_second_requester_rejected);
    RUN_TEST(test_slot_reusable_after_end);
    RUN_TEST(test_end_by_non_owner_is_noop);
    RUN_TEST(test_stale_claim_self_heals);
    RUN_TEST(test_millis_wraparound);
    return UNITY_END();
}
