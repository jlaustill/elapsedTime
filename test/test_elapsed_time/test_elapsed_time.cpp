#include <unity.h>

#include <ElapsedTime.h>

// A clock the tests drive by hand. Every assertion below is exact rather than
// approximate because time only moves when a test moves it.
static uint32_t fakeNow = 0;
extern "C" uint32_t fakeClock(void) { return fakeNow; }

// A tick that is not a clock at all. Nothing in the library assumes time --
// only a monotonically increasing u32 -- so the same timer counts executions.
static uint32_t callCount = 0;
extern "C" uint32_t countingTick(void) { return callCount; }

// Mirrors how a user declares a timer in C-Next:
//
//     ElapsedTime.Timer elapsed250 <- { dueEvery: 250, tick: millis };
//
// Two fields written and the rest zero-filled -- C99 6.7.9p21 for the
// designated initializer C-Next emits, `= {}` here. A test never names an
// internal field, for the same reason a user never does.
static ElapsedTime__Timer makeTimer(uint32_t dueEvery = 0,
                                    ElapsedTime__tickSource_fp tick = fakeClock) {
    ElapsedTime__Timer t = {};
    t.dueEvery = dueEvery;
    t.tick = tick;
    return t;
}

void setUp(void) { fakeNow = 0; callCount = 0; }
void tearDown(void) {}

// ------------------------------------------------------------ lazy seeding

void test_fresh_timer_against_a_running_clock_reports_no_elapsed_time(void) {
    // The whole reason `started` exists: a timer declared while millis()
    // already reads 40000 must not claim 40 seconds that never elapsed.
    fakeNow = 40000;
    ElapsedTime__Timer t = makeTimer(250);
    TEST_ASSERT_EQUAL_UINT64(0u, ElapsedTime__elapsed(&t));
}

void test_fresh_timer_fires_one_full_interval_after_first_touch(void) {
    // Not immediately, and not at 250 on a clock that started at 40000.
    fakeNow = 40000;
    ElapsedTime__Timer t = makeTimer(250);
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));      // seeds the origin here
    fakeNow = 40249;
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
    fakeNow = 40250;
    TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
}

// ------------------------------------------------------- the safety fold

// Folding is private and automatic in v2, so the one thing a user can observe
// about it is that they cannot observe it: elapsed() must not jump at 2^31.
void test_elapsed_is_continuous_across_the_safety_fold(void) {
    ElapsedTime__Timer t = makeTimer();
    fakeNow = 0; ElapsedTime__elapsed(&t);              // seeds at 0
    fakeNow = 2147483648u; ElapsedTime__elapsed(&t);    // 2^31 -- folds here
    // Tick by tick across the u32 ceiling. Every value below is past what a
    // u32 could express, so each one needs the fold to have happened and to
    // have counted exactly once -- a double-counted fold breaks these.
    fakeNow = 4294967295u;
    TEST_ASSERT_EQUAL_UINT64(4294967295ull, ElapsedTime__elapsed(&t));
    fakeNow = 0u;                                       // the clock wraps
    TEST_ASSERT_EQUAL_UINT64(4294967296ull, ElapsedTime__elapsed(&t));
    fakeNow = 1u;
    TEST_ASSERT_EQUAL_UINT64(4294967297ull, ElapsedTime__elapsed(&t));
}

// What the fold buys: no 49.7-day ceiling, with no call for a user to forget.
// Six laps of two billion is three full u32 wraps -- inexpressible in a u32.
void test_elapsed_is_unbounded_without_any_call_from_the_user(void) {
    ElapsedTime__Timer t = makeTimer();
    fakeNow = 0; ElapsedTime__elapsed(&t);
    for (int lap = 0; lap < 6; lap++) {
        fakeNow += 2000000000u;
        ElapsedTime__elapsed(&t);
    }
    TEST_ASSERT_EQUAL_UINT64(12000000000ull, ElapsedTime__elapsed(&t));
}

// --------------------------------------------------------------- hasElapsed

void test_has_elapsed_is_false_before_the_threshold_and_true_at_it(void) {
    ElapsedTime__Timer t = makeTimer();
    fakeNow = 0; ElapsedTime__elapsed(&t);
    fakeNow = 4999;
    TEST_ASSERT_FALSE(ElapsedTime__hasElapsed(&t, 5000u));
    fakeNow = 5000;
    TEST_ASSERT_TRUE(ElapsedTime__hasElapsed(&t, 5000u));
}

// It answers a question without changing the answer to the next one -- which
// is what separates it from isDue.
void test_has_elapsed_does_not_advance_the_timer(void) {
    ElapsedTime__Timer t = makeTimer();
    fakeNow = 0; ElapsedTime__elapsed(&t);
    fakeNow = 5000;
    TEST_ASSERT_TRUE(ElapsedTime__hasElapsed(&t, 5000u));
    TEST_ASSERT_TRUE(ElapsedTime__hasElapsed(&t, 5000u));   // still true
    TEST_ASSERT_EQUAL_UINT64(5000u, ElapsedTime__elapsed(&t));
}

// ----------------------------------------------------------------- remaining

void test_remaining_counts_down_and_floors_at_zero_when_overdue(void) {
    ElapsedTime__Timer t = makeTimer(250);
    fakeNow = 0; ElapsedTime__isDue(&t);                    // seeds at 0
    TEST_ASSERT_EQUAL_UINT32(250u, ElapsedTime__remaining(&t));
    fakeNow = 100;
    TEST_ASSERT_EQUAL_UINT32(150u, ElapsedTime__remaining(&t));
    fakeNow = 250;
    TEST_ASSERT_EQUAL_UINT32(0u, ElapsedTime__remaining(&t));
    fakeNow = 9999;                                         // long overdue
    TEST_ASSERT_EQUAL_UINT32(0u, ElapsedTime__remaining(&t));
}

// u32max is the identity for minimum, so a never-due timer does not constrain
// a caller taking the minimum across several timers to size a sleep. Returning
// 0 would make every never-due timer forbid sleeping entirely.
void test_remaining_is_u32max_when_the_timer_is_never_due(void) {
    ElapsedTime__Timer t = makeTimer(0);
    fakeNow = 1000;
    TEST_ASSERT_EQUAL_UINT32(4294967295u, ElapsedTime__remaining(&t));
}

// --------------------------------------------------------- reset and resetTo

void test_reset_reads_the_clock_and_clears_the_cumulative(void) {
    ElapsedTime__Timer t = makeTimer();
    fakeNow = 0; ElapsedTime__elapsed(&t);
    fakeNow = 900;
    TEST_ASSERT_EQUAL_UINT64(900u, ElapsedTime__elapsed(&t));
    ElapsedTime__reset(&t);
    TEST_ASSERT_EQUAL_UINT64(0u, ElapsedTime__elapsed(&t));
    fakeNow = 1000;
    TEST_ASSERT_EQUAL_UINT64(100u, ElapsedTime__elapsed(&t));
}

// One clock reading shared by several timers aligns them exactly, rather than
// letting them drift by the ticks between separate reset() calls.
void test_reset_to_aligns_several_timers_on_one_reading(void) {
    ElapsedTime__Timer a = makeTimer(250);
    ElapsedTime__Timer b = makeTimer(500);
    fakeNow = 7777;
    uint32_t origin = fakeClock();
    ElapsedTime__resetTo(&a, origin);
    fakeNow = 7800;                          // time moves between the two calls
    ElapsedTime__resetTo(&b, origin);        // but both share the one reading
    fakeNow = 8777;
    TEST_ASSERT_EQUAL_UINT64(1000u, ElapsedTime__elapsed(&a));
    TEST_ASSERT_EQUAL_UINT64(1000u, ElapsedTime__elapsed(&b));
}

// --------------------------------------------------------------------- isDue

void test_fires_exactly_at_the_boundary(void) {
    ElapsedTime__Timer t = makeTimer(250);
    fakeNow = 0; ElapsedTime__isDue(&t);
    fakeNow = 250;
    TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
}

void test_does_not_fire_one_tick_short(void) {
    ElapsedTime__Timer t = makeTimer(250);
    fakeNow = 0; ElapsedTime__isDue(&t);
    fakeNow = 249;
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
}

void test_does_not_fire_again_immediately(void) {
    ElapsedTime__Timer t = makeTimer(250);
    fakeNow = 0; ElapsedTime__isDue(&t);
    fakeNow = 250;
    TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
}

// A handler that takes almost the whole interval must not shift the cadence,
// because isDue advances BEFORE the handler runs.
void test_slow_handler_does_not_shift_cadence(void) {
    ElapsedTime__Timer t = makeTimer(250);
    fakeNow = 0; ElapsedTime__isDue(&t);

    fakeNow = 250; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    fakeNow = 499; TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));   // handler took 249
    fakeNow = 500; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    fakeNow = 749; TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
    fakeNow = 750; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
}

// A handler slower than its own interval self-limits instead of building a
// backlog. This is what advancing to now buys over advancing by one interval.
void test_handler_slower_than_interval_never_fires_twice_running(void) {
    ElapsedTime__Timer t = makeTimer(250);
    fakeNow = 0; ElapsedTime__isDue(&t);

    fakeNow = 250; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    fakeNow = 550; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));    // handler took 300
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));                  // not twice running
    fakeNow = 850; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
}

// The case that separates this design from advance-by-interval: a long stall
// yields ONE firing, not one per missed slot.
void test_long_stall_fires_once_not_once_per_missed_slot(void) {
    ElapsedTime__Timer t = makeTimer(250);
    fakeNow = 0; ElapsedTime__isDue(&t);

    fakeNow = 1000;                                  // four intervals missed
    TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
}

void test_fires_across_a_clock_wrap(void) {
    ElapsedTime__Timer t = makeTimer(250);
    fakeNow = 4294967200u; ElapsedTime__isDue(&t);                      // seeds
    fakeNow = 4294967295u; TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));   // 95 elapsed
    // 95 ticks to the ceiling, +1 across it, +153 after = 249 -- one short.
    fakeNow = 153u; TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
    fakeNow = 154u; TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
}

void test_due_every_zero_never_fires(void) {
    ElapsedTime__Timer t = makeTimer(0);
    fakeNow = 0; ElapsedTime__elapsed(&t);
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
    fakeNow = 4294967295u;
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));
}

// isDue folds rather than resets, so elapsed() stays a true cumulative on a
// polled timer instead of being zeroed on every firing.
void test_elapsed_survives_repeated_firings(void) {
    ElapsedTime__Timer t = makeTimer(250);
    fakeNow = 0; ElapsedTime__isDue(&t);
    for (uint32_t fired = 1; fired <= 40; fired++) {
        fakeNow += 250;
        TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
    }
    TEST_ASSERT_EQUAL_UINT64(10000u, ElapsedTime__elapsed(&t));
}

// The asymmetry the design calls out: isDue deliberately does NOT share the
// safety fold, and a later reader must not "unify" the two paths.
//
// The case that catches unification is a long interval polled LATE -- past the
// interval AND past the 2^31 fold threshold. A shared fold would zero `since`
// before the due check ever ran, and the timer would never fire at all.
// Polling on time never reaches 2^31, so it cannot detect the difference.
void test_a_long_interval_polled_late_still_fires(void) {
    ElapsedTime__Timer t = makeTimer(2147483000u);   // just under 2^31
    fakeNow = 0; ElapsedTime__isDue(&t);
    fakeNow = 2147482999u;
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));       // one tick short
    fakeNow = 3000000000u;                           // past the fold threshold
    TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));
}

// ------------------------------------------------- the tick need not be time

// Nothing in the library assumes time -- only a monotonically increasing u32 --
// so a counter the caller increments works exactly as well as a clock. What
// these really pin is the ABSENCE of an assumption: a later refactor that
// quietly starts treating the tick as time breaks them immediately.
void test_counts_executions_when_the_tick_is_a_counter(void) {
    ElapsedTime__Timer everyTenth = makeTimer(10, countingTick);
    callCount = 0; ElapsedTime__isDue(&everyTenth);      // seeds at 0

    uint32_t fired = 0;
    for (uint32_t execution = 1; execution <= 100; execution++) {
        callCount = execution;
        if (ElapsedTime__isDue(&everyTenth)) { fired++; }
    }
    TEST_ASSERT_EQUAL_UINT32(10u, fired);
    TEST_ASSERT_EQUAL_UINT64(100u, ElapsedTime__elapsed(&everyTenth));
}

void test_counter_wrap_behaves_like_a_clock_wrap(void) {
    ElapsedTime__Timer t = makeTimer(10, countingTick);
    callCount = 4294967295u; ElapsedTime__isDue(&t);     // seeds at the ceiling
    callCount = 8u;
    TEST_ASSERT_FALSE(ElapsedTime__isDue(&t));           // 9 counted
    callCount = 9u;
    TEST_ASSERT_TRUE(ElapsedTime__isDue(&t));            // 10 counted
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_fresh_timer_against_a_running_clock_reports_no_elapsed_time);
    RUN_TEST(test_fresh_timer_fires_one_full_interval_after_first_touch);
    RUN_TEST(test_elapsed_is_continuous_across_the_safety_fold);
    RUN_TEST(test_elapsed_is_unbounded_without_any_call_from_the_user);
    RUN_TEST(test_has_elapsed_is_false_before_the_threshold_and_true_at_it);
    RUN_TEST(test_has_elapsed_does_not_advance_the_timer);
    RUN_TEST(test_remaining_counts_down_and_floors_at_zero_when_overdue);
    RUN_TEST(test_remaining_is_u32max_when_the_timer_is_never_due);
    RUN_TEST(test_reset_reads_the_clock_and_clears_the_cumulative);
    RUN_TEST(test_reset_to_aligns_several_timers_on_one_reading);
    RUN_TEST(test_fires_exactly_at_the_boundary);
    RUN_TEST(test_does_not_fire_one_tick_short);
    RUN_TEST(test_does_not_fire_again_immediately);
    RUN_TEST(test_slow_handler_does_not_shift_cadence);
    RUN_TEST(test_handler_slower_than_interval_never_fires_twice_running);
    RUN_TEST(test_long_stall_fires_once_not_once_per_missed_slot);
    RUN_TEST(test_fires_across_a_clock_wrap);
    RUN_TEST(test_due_every_zero_never_fires);
    RUN_TEST(test_elapsed_survives_repeated_firings);
    RUN_TEST(test_a_long_interval_polled_late_still_fires);
    RUN_TEST(test_counts_executions_when_the_tick_is_a_counter);
    RUN_TEST(test_counter_wrap_behaves_like_a_clock_wrap);
    return UNITY_END();
}
