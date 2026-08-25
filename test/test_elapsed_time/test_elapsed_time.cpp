#include <unity.h>

#include <ElapsedTime.h>

// A clock the tests drive by hand. Every assertion below is exact rather than
// approximate because time only moves when a test moves it.
static uint32_t fakeNow = 0;
extern "C" uint32_t fakeClock(void) { return fakeNow; }

static ElapsedTime__Config makeTimer(uint32_t dueEvery = 0,
                                     uint32_t rollOverAt = 0) {
    ElapsedTime__Config t;
    t.total = 0;
    t.startedAtTick = 0;
    t.rollOverAt = rollOverAt;
    t.dueEvery = dueEvery;
    t.tick = fakeClock;
    return t;
}

void setUp(void) { fakeNow = 0; }
void tearDown(void) {}

// ---------------------------------------------------------------- timeSince

void test_plain_difference(void) {
    ElapsedTime__Config t = makeTimer();
    fakeNow = 1000; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 1500;
    TEST_ASSERT_EQUAL_UINT32(500u, ElapsedTime__timeSince(&t));
}

void test_zero_when_no_time_has_passed(void) {
    ElapsedTime__Config t = makeTimer();
    fakeNow = 1000; ElapsedTime__seed(&t, fakeNow);
    TEST_ASSERT_EQUAL_UINT32(0u, ElapsedTime__timeSince(&t));
}

void test_spans_a_clock_wrap(void) {
    ElapsedTime__Config t = makeTimer();
    // seeded 295 ticks before the ceiling, read 204 ticks after zero
    fakeNow = 4294967000u; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 204u;
    TEST_ASSERT_EQUAL_UINT32(500u, ElapsedTime__timeSince(&t));
}

void test_one_tick_across_the_ceiling(void) {
    ElapsedTime__Config t = makeTimer();
    fakeNow = 4294967295u; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 0u;
    TEST_ASSERT_EQUAL_UINT32(1u, ElapsedTime__timeSince(&t));
}

void test_largest_measurable_interval_does_not_saturate(void) {
    ElapsedTime__Config t = makeTimer();
    fakeNow = 4294967295u; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 4294967294u;
    TEST_ASSERT_EQUAL_UINT32(4294967295u, ElapsedTime__timeSince(&t));
}

// -------------------------------------------------------------------- value

void test_value_is_total_plus_time_since(void) {
    ElapsedTime__Config t = makeTimer(0, 100);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 500; ElapsedTime__handleOverflow(&t);   // folds 500
    fakeNow = 700;
    TEST_ASSERT_EQUAL_UINT64(700u, ElapsedTime__value(&t));
}

// --------------------------------------------------------------- seed/reset

void test_seed_sets_the_origin_and_clears_total(void) {
    ElapsedTime__Config t = makeTimer(0, 0);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 900; ElapsedTime__handleOverflow(&t);
    fakeNow = 1000; ElapsedTime__seed(&t, fakeNow);
    TEST_ASSERT_EQUAL_UINT64(0u, ElapsedTime__value(&t));
}

void test_reset_reads_the_clock(void) {
    ElapsedTime__Config t = makeTimer();
    fakeNow = 4242; ElapsedTime__reset(&t);
    TEST_ASSERT_EQUAL_UINT32(0u, ElapsedTime__timeSince(&t));
    fakeNow = 4342;
    TEST_ASSERT_EQUAL_UINT32(100u, ElapsedTime__timeSince(&t));
}

// ----------------------------------------------------------- handleOverflow

void test_fold_moves_elapsed_into_total(void) {
    ElapsedTime__Config t = makeTimer(0, 100);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 500; ElapsedTime__handleOverflow(&t);
    TEST_ASSERT_EQUAL_UINT32(0u, ElapsedTime__timeSince(&t));
    TEST_ASSERT_EQUAL_UINT64(500u, ElapsedTime__value(&t));
}

void test_fold_below_the_threshold_does_nothing(void) {
    ElapsedTime__Config t = makeTimer(0, 1000);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 500; ElapsedTime__handleOverflow(&t);
    TEST_ASSERT_EQUAL_UINT32(500u, ElapsedTime__timeSince(&t));
}

// The property the whole design exists for: folding must lose nothing.
void test_repeated_folds_lose_no_time(void) {
    ElapsedTime__Config t = makeTimer(0, 0);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    for (uint32_t step = 1; step <= 1000; step++) {
        fakeNow += 37;                       // an awkward, non-round interval
        ElapsedTime__handleOverflow(&t);
    }
    TEST_ASSERT_EQUAL_UINT64(37000u, ElapsedTime__value(&t));
}

void test_folding_past_the_u32_ceiling_is_unbounded(void) {
    ElapsedTime__Config t = makeTimer(0, 0);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    // three full wraps plus a remainder -- impossible for a u32 timer
    for (int lap = 0; lap < 6; lap++) {
        fakeNow += 2000000000u;
        ElapsedTime__handleOverflow(&t);
    }
    TEST_ASSERT_EQUAL_UINT64(12000000000ull, ElapsedTime__value(&t));
}

// -------------------------------------------------------------------- isDue

void test_fires_exactly_at_the_boundary(void) {
    ElapsedTime__Config t = makeTimer(250);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 250;
    TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
}

void test_does_not_fire_one_tick_short(void) {
    ElapsedTime__Config t = makeTimer(250);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 249;
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
}

void test_does_not_fire_again_immediately(void) {
    ElapsedTime__Config t = makeTimer(250);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 250;
    TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
}

// A handler that takes almost the whole interval must not shift the cadence,
// because isDue advances BEFORE the handler runs.
void test_slow_handler_does_not_shift_cadence(void) {
    ElapsedTime__Config t = makeTimer(250);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);

    fakeNow = 250; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    fakeNow = 499; TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));   // handler took 249
    fakeNow = 500; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    fakeNow = 749; TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
    fakeNow = 750; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
}

// A handler slower than its own interval self-limits instead of building a
// backlog. This is what advancing to now buys over advancing by one interval.
void test_handler_slower_than_interval_never_fires_twice_running(void) {
    ElapsedTime__Config t = makeTimer(250);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);

    fakeNow = 250; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    fakeNow = 550; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));    // handler took 300
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));                  // not twice running
    fakeNow = 850; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
}

// The case that separates this design from advance-by-interval: a long stall
// yields ONE firing, not one per missed slot.
void test_long_stall_fires_once_not_once_per_missed_slot(void) {
    ElapsedTime__Config t = makeTimer(250);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);

    fakeNow = 1000;                                  // four intervals missed
    TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
}

void test_fires_across_a_clock_wrap(void) {
    ElapsedTime__Config t = makeTimer(250);
    fakeNow = 4294967200u; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 4294967295u; TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));   // 95 elapsed
    // 95 ticks to the ceiling, +1 across it, +153 after = 249 -- one short.
    fakeNow = 153u;        TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
    fakeNow = 154u;        TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));    // exactly 250
}

void test_due_every_zero_never_fires(void) {
    ElapsedTime__Config t = makeTimer(0);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    fakeNow = 4000000000u;
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
}

// isDue folds rather than resets, so a polled timer still reports true uptime.
void test_value_survives_repeated_firings(void) {
    ElapsedTime__Config t = makeTimer(250);
    fakeNow = 0; ElapsedTime__seed(&t, fakeNow);
    for (int fire = 1; fire <= 40; fire++) {
        fakeNow = (uint32_t)(fire * 250);
        TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    }
    TEST_ASSERT_EQUAL_UINT64(10000u, ElapsedTime__value(&t));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_plain_difference);
    RUN_TEST(test_zero_when_no_time_has_passed);
    RUN_TEST(test_spans_a_clock_wrap);
    RUN_TEST(test_one_tick_across_the_ceiling);
    RUN_TEST(test_largest_measurable_interval_does_not_saturate);
    RUN_TEST(test_value_is_total_plus_time_since);
    RUN_TEST(test_seed_sets_the_origin_and_clears_total);
    RUN_TEST(test_reset_reads_the_clock);
    RUN_TEST(test_fold_moves_elapsed_into_total);
    RUN_TEST(test_fold_below_the_threshold_does_nothing);
    RUN_TEST(test_repeated_folds_lose_no_time);
    RUN_TEST(test_folding_past_the_u32_ceiling_is_unbounded);
    RUN_TEST(test_fires_exactly_at_the_boundary);
    RUN_TEST(test_does_not_fire_one_tick_short);
    RUN_TEST(test_does_not_fire_again_immediately);
    RUN_TEST(test_slow_handler_does_not_shift_cadence);
    RUN_TEST(test_handler_slower_than_interval_never_fires_twice_running);
    RUN_TEST(test_long_stall_fires_once_not_once_per_missed_slot);
    RUN_TEST(test_fires_across_a_clock_wrap);
    RUN_TEST(test_due_every_zero_never_fires);
    RUN_TEST(test_value_survives_repeated_firings);
    return UNITY_END();
}
