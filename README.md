# elapsedTime

Portable, tick-agnostic elapsed-time timers for [C-Next](https://github.com/jlaustill/c-next) — the ergonomics behind Teensy's `elapsedMillis`, for **any MCU**, **any tick source**, and with **no 49.7-day ceiling**.

> **Status: implemented, 36/36 tests passing.** Requires C-Next 0.4.0 or
> later — see [Requirements](#requirements).

## Why

`elapsedMillis` is one of the most useful things in the Teensy core:

```cpp
elapsedMillis elapsed250;
if (elapsed250 > 250) { elapsed250 = 0; fireThe250msThings(); }
```

Three things stop it going further, and this library fixes all three.

| | elapsedMillis | elapsedTime |
| --- | --- | --- |
| Clock | hard-coded `millis()` / `micros()` | any `u32` tick you supply |
| Platform | Teensy, or Arduino with `micros()` | anything with a free-running `u32` counter |
| Ceiling | 49.7 days, silently wrong past it | unbounded, automatic |
| Units | two duplicate classes | one type |

## Usage

```cnx
mut ElapsedTime.Timer elapsed250 <- { dueEvery: 250, tick: millis };

void loop() {
    bool due <- ElapsedTime.isDue(elapsed250);
    if (due = true) {
        fireThe250msThings();
    }
}
```

That declaration is the whole setup. There is no constructor, no `setup()` step and no periodic service call — **if a user can forget it, the library does not need it.**

`isDue` is the whole pattern in one call: it reports whether the interval has elapsed and advances the timer if it has, so there is no reset to forget, and the interval lives with the timer rather than being repeated at every call site.

(Two lines rather than one because C-Next forbids a function call inside an `if` condition — MISRA C:2012 Rule 13.5, `E0702`. And `mut` because every call writes the timer: C-Next makes a binding read-only unless it is declared `mut` — ADR-071, `E0900`.)

Uptime past 49.7 days needs no extra call — folding is internal and automatic, as long as the timer is read at least once per half range of its tick (see [Constraints](#constraints)):

```cnx
u64 uptime <- ElapsedTime.elapsed(elapsedSystemTime);
```

## Any tick source

The clock is a caller-supplied callback, so the library never names a platform:

| Platform | Tick source |
| --- | --- |
| Arduino (AVR, ESP32, STM32, RP2040, SAMD, Teensy) | `millis()` / `micros()` |
| STM32 HAL | `HAL_GetTick()` — same type, same units, same wrap |
| FreeRTOS | `xTaskGetTickCount()` via a `u32` wrapper, plus `tickBitWidth: 16` when ticks are 16-bit (`configTICK_TYPE_WIDTH_IN_BITS = TICK_TYPE_WIDTH_16_BITS`, or the older `configUSE_16_BIT_TICKS = 1`) |
| Zephyr | `k_cycle_get_32()` |
| Cortex-M3/M4/M7 | `DWT->CYCCNT` |
| Host tests | a fake clock you advance by hand |

It also means the library is not embedded-specific at all. Nothing in it names a framework, so it builds and runs on a host — the entire test suite runs natively against a fake clock, no board and no cross-compiler involved. A desktop application, a server, a game loop or a wasm build can use it exactly as an MCU does.

### A tick narrower than 32 bits

The library expects a tick that counts all the way to `4294967295` before wrapping to 0. A tick that wraps sooner — a 16-bit hardware timer, a 24-bit counter, a timer with an auto-reload — must declare its range beside its clock. Otherwise its first wrap reports an elapsed time billions of ticks too long. Declare it whichever way is natural:

```cnx
mut ElapsedTime.Timer sample <- { dueEvery: 100, tick: timer1Count, tickBitWidth: 16 };
mut ElapsedTime.Timer frame  <- { dueEvery: 10,  tick: tim2Count,   tickMaxValue: 999 };  // ARR = 999
```

- **`tickBitWidth`** covers a counter that wraps at a power of two: 8, 16, 24. `0`, and anything from 32 up, means the full `u32` range.
- **`tickMaxValue`** is the last value before the wrap, and covers the rest: a timer that reloads at 999 counts 0..999. `0` means the full `u32` range.
- **If both are set, `tickMaxValue` wins**, because it is the more exact of the two.

A narrow tick tightens the one limit that scales with the range (see [Constraints](#constraints)): an 8-bit tick at 1 kHz must be read at least every 128 ms. Its `dueEvery` is not limited — a 1 MHz timer with `tickMaxValue: 999` and `dueEvery: 5000` fires every 5 ms.

The tick must also count *up*. ARM SysTick's `VAL` counts down from its reload value, so a range alone does not fix it: wrap it as `reload - SysTick->VAL` and declare `tickMaxValue: reload`.

### The tick need not be a clock

Nothing in the library assumes time. `sinceTick` needs only a monotonically increasing `u32`, so a counter works exactly as well as a clock — feed it a value your own code increments and `dueEvery: 100` means *every hundredth execution*, while `elapsed()` becomes a total execution count, unbounded past 2³² exactly as time is.

```cnx
mut u32 framesSeen <- 0;                    // incremented by the code being counted
u32 frameTick() { return framesSeen; }      // a pure READ

mut ElapsedTime.Timer everyHundredthFrame <- { dueEvery: 100, tick: frameTick };
```

The increment belongs in the code being counted; the tick function must stay a pure read. A tick that incremented on read would count *observations* rather than executions, and every `elapsed()` call would inflate it.

Pairing two timers on different ticks is where this earns its keep: a time-based timer at 1 Hz plus a counting one gives loop rate — iterations per second — with no extra machinery.

Because the clock is **per-timer**, milliseconds and microseconds coexist in one program — which also collapses `elapsedMillis` and `elapsedMicros` into a single type instead of two byte-for-byte duplicates.

## API

```cnx
scope ElapsedTime {
    // ADR-029: a function definition creates both a function and a type, and a field
    // of that type initialises to it -- so the clock is never null.
    public u32 tickSource() { return 0; }

    public struct Timer {
        u32 dueEvery;        // isDue interval; 0 = never due
        tickSource tick;     // the clock
        u32 tickMaxValue;    // the tick's last value before it wraps; 0 = u32 max
        u8  tickBitWidth;    // or its width in bits; 0 = 32
        u64 total;           // internal -- folded elapsed
        u64 lastFiredAt;     // internal -- elapsed() when isDue last fired
        u32 startedAtTick;   // internal -- origin of the current interval
        bool started;        // internal -- false until the first touch
    }

    public bool isDue(mut Timer timer);                  // due? if so, advance and say true
    public u64  elapsed(mut Timer timer);                // total elapsed, unbounded
    public bool hasElapsed(mut Timer timer, u32 ticks);  // non-advancing threshold test
    public u32  remaining(mut Timer timer);              // ticks until due
    public void reset(mut Timer timer);                  // origin <- tick()
    public void resetTo(mut Timer timer, u32 now);       // origin <- now
}
```

Only the first fields are ever written at a call site — `dueEvery` and `tick`, plus `tickMaxValue` or `tickBitWidth` for a narrow tick; the rest zero-fill (C99 6.7.9p21). Field order is deliberate — the struct reads in the order it is typed. A timer is 40 bytes on 32-bit targets such as Cortex-M and ESP32, and 32 on AVR, measured as `sizeof` with each target's own compiler. The tick's range costs 8 of those on a 32-bit target, where the `u8` width brings three bytes of padding ahead of the `u64`s, and 5 on AVR. `lastFiredAt` costs 8 on both.

## Design notes

**Elapsed time is computed, never stored.** Every read derives `tick() - startedAtTick` afresh, so a timer cannot go stale and needs no per-loop update step.

**A timer seeds itself on first touch.** C-Next has no constructors (ADR-005) and a global's initializer must be a compile-time constant, so a timer cannot capture `millis()` where it is declared. A timer left at `startedAtTick: 0` while the clock already reads 40,000 would fire immediately and report 40 seconds that never elapsed. The internal `started` flag is false until the first call, which seeds the origin from the clock and reports not-yet; the first firing lands one full interval later. Seeding lives in one private function, so `isDue` and `elapsed` cannot disagree about what a fresh timer means.

**Wrap handling is one visible branch**, not a property of the arithmetic. Every tick wraps — a `u32` one at 49.7 days of milliseconds or 71.6 minutes of microseconds, a narrower one far sooner. Teensy's version stays correct across that only because C's unsigned subtraction wraps; C-Next defaults to clamp arithmetic, which saturates instead and would stall every timer for a full wrap period. So it is explicit, and it counts up to the tick's own ceiling — `4294967295` unless the timer declares a narrower range:

```cnx
if (now >= startedAtTick) { return now - startedAtTick; }
return (ceiling - startedAtTick) + now + 1;
```

That expression cannot overflow: the second branch runs only when `now < startedAtTick`, so its result is at most `ceiling` exactly. The `+ 1` is the step from the ceiling back to 0: `250` to `5` on an 8-bit tick is 11 ticks, not 10. C-Next still wraps the arithmetic in its saturating `cnx_clamp_*` helpers. By that bound they never fire, and they stay anyway: if the branch is ever wrong, or a tick reads above the range it declared, a reading that saturates fails safer than one that wraps.

**`isDue` advances before the handler runs, and that ordering is the design.** It reads the clock once, and if the interval has elapsed it records the firing *before* returning `true` — so the caller's handler executes on already-advanced state and its duration cannot push the next firing:

```
t=250   isDue -> fires, lastFiredAt <- 250
        handler runs 249 ms
t=499   isDue -> since 249 -> false
t=500   isDue -> since 250 -> fires
```

Firings land at 250, 500, 750. The same two statements in the other order drift by the handler's duration every time, which is why the sequencing is commented in the implementation rather than left looking arbitrary.

**It advances to *now*, not by exactly one interval.** Advancing by the interval is drift-free against late polling, but when a handler outruns its interval it either silently skips a slot or fires repeatedly to catch up. Advancing to now does neither: it never skips, never fires twice in a row, and when the handler is genuinely slower than the interval it self-limits to the handler's real rate instead of building a backlog. The only residual drift is polling latency, which is microseconds when `isDue` runs each loop.

**`dueEvery: 0` means never due**, not always due. A zero-initialised timer firing on every call would be a trap, and `0 >= 0` is true for unsigned values.

**`isDue` records rather than resets**, so `elapsed()` stays a true cumulative on a polled timer. A firing notes where the cumulative stood; `reset()` would zero `total` and leave `elapsed()` permanently near zero on anything periodic.

**Overflow handling is automatic and private.** The proof of concept made the caller run `handleOverflow()` periodically to push past the 49.7-day ceiling — a step a user can forget, and forgetting it was silent. Folding now happens inside the accessors at **half the tick's range** — 2^31 on a full `u32` tick, which is 24.8 days at milliseconds and 35.8 minutes at microseconds; 128 on an 8-bit tick — for one `u64` add per fold. The threshold has to follow the range: at a fixed 2^31 a narrow tick would never fold, and would lose a whole range on every wrap. The fold restarts the interval from the same tick reading, so the intervals abut exactly and **no time is lost at the boundary**.

**`isDue` counts on the cumulative, so no read can swallow a firing.** It fires once `elapsed()` has grown `dueEvery` past where it last fired. The cumulative is the one quantity the safety fold keeps exact. An earlier version compared `dueEvery` with the raw interval since the origin, which any fold restarts, and that failed two ways:
- A fold run by `elapsed()`, `hasElapsed()` or `remaining()` silently swallowed a due firing. On an 8-bit tick with `dueEvery: 100`, reads at 120 and 128 moved the next firing from 128 to 228.
- A narrow tick's raw interval never exceeds its ceiling, so a `dueEvery` at or above the range never fired at all.

A test pins each, and another polls a long interval late, past both the interval and the fold threshold, to show the fold is invisible to `isDue`.

**`hasElapsed` runs on the `u64`.** It tests total elapsed against a threshold without advancing the timer, which serves one-shots that have no `dueEvery`. It is not a cheap `u32` path — "elapsed since reset" is `total + since`, inherently `u64`. The justification is reach: a `u64` of milliseconds tops out 584 million years away, so a threshold of a year is a legitimate call on hardware that will never reach its own ceiling.

**`remaining()` returns `u32max` when a timer is never due.** The use case is low-power scheduling, where a caller takes the *minimum* across several timers to size a sleep. `u32max` is the identity for minimum, so a never-due timer correctly does not constrain the sleep; returning 0 would make every never-due timer forbid sleeping entirely.

**`resetTo()` exists for exact origins.** `reset()` reads the clock; `resetTo()` takes the reading from you. One reading shared across several timers aligns them exactly, rather than letting them drift by the ticks between separate `reset()` calls.

**Every accessor mutates, so every one takes its timer `mut`.** Folding is a write, even in `hasElapsed()` and `remaining()`, so a timer is declared `mut` wherever it lives. That is honest rather than unfortunate: a timer is a stateful object, and `elapsedMillis` behaves the same way.

**Each timer is independent.** A module in another file owns its own timer, and nothing is shared, so nothing coordinates.

### Constraints

The one limit is half the tick's range: 2^31 on a full `u32` tick, which is 24.8 days at milliseconds and 35.8 minutes at microseconds, and 128 on an 8-bit tick. `dueEvery` is not limited by the range — any `u32` interval works on any tick, because `isDue` counts on the cumulative.

**A timer must be read at least once per half range.** Every accessor counts as a read, `isDue` included. The wrap branch can only measure a distance shorter than one range, and a timer read just under the fold threshold has only half a range left before that. A longer gap between reads loses a whole range, and nothing can detect it. On an 8-bit tick, a read at 127 followed by one 129 ticks later reports 0 instead of 256.

## Requirements

**C-Next [0.4.0](https://github.com/jlaustill/c-next/releases/tag/v0.4.0) or later.** No earlier release can transpile this library:

- `0.3.0` lacks [c-next#1207](https://github.com/jlaustill/c-next/pull/1207): a function-as-type field in a scope-nested struct emitted no function-pointer typedef, so the generated C did not compile. It also emits a raw name that is not a type where `Timer` names `tickSource` bare from inside the same scope; the header must emit `ElapsedTime__tickSource_fp`. That fix was opened as [c-next#1281](https://github.com/jlaustill/c-next/pull/1281), closed unmerged after being blocked on [#1285](https://github.com/jlaustill/c-next/issues/1285), and reached `main` through that refactor instead.
- `0.3.1` rejects every parenthesized comparison in a condition with E0701 ([c-next#1195](https://github.com/jlaustill/c-next/issues/1195)), and `tickCeiling` writes `if ((width = 0) || (width >= 32))`. It also leaves the `u64` fold `total +<- since` unclamped, where `0.4.0` emits `cnx_clamp_add_u64`.
- Both predate `mut` ([ADR-071](https://github.com/jlaustill/c-next/blob/main/docs/decisions/adr-071-read-only-by-default.md)), which `0.4.0` added and requires on every binding that is written — here, the timer parameter of every function that writes it or passes it on.

The committed `.c`/`.h` were generated by C-Next `0.4.0`, and regenerating reproduces them byte for byte. A read-only binding is now emitted with C's `const`. In the header that only adds `const` to the by-value `u32` parameters of `hasElapsed` and `resetTo`, which is not part of a C function's type (C11 6.7.6.3p15), so existing callers and the ABI are unchanged.

**One set of generated files serves every target.** ADR-049 has every program name exactly one target, so `cnext.config.json` names `host`, which matches the native test suite. Here the choice changes nothing: generating for each of the 13 targets C-Next knows (`host`, AVR, ESP32, Cortex-M0 through M7, STM32F4, Teensy 4.x) gives a byte-identical `.c` and `.h`, because the library uses nothing target-specific. If that changes — an `atomic` tick, say — the output will differ per target, and the library will ship one set per target.

Two further defects found while designing this shaped the API rather than blocking it:

- [c-next#1202](https://github.com/jlaustill/c-next/issues/1202) — a global's initialiser must be a compile-time constant, so a timer cannot capture its clock where it is declared. This is why a timer seeds itself on first touch rather than through a builder.
- [c-next#1215](https://github.com/jlaustill/c-next/issues/1215) — a callback-typed *scope member* without an explicit initialiser is rejected as uninitialised. Not hit here: the clock is a struct field, not a scope member.

## Testing

```bash
pio test -e native
```

36 tests against a clock the suite drives by hand, so every assertion is exact rather than approximate. The cases that carry the design:

| Test | What it pins |
| --- | --- |
| `fresh_timer_against_a_running_clock_reports_no_elapsed_time` | a timer declared at t=40000 claims 0, not 40 seconds |
| `fresh_timer_fires_one_full_interval_after_first_touch` | lazy seeding — first firing at 40250, not immediately |
| `elapsed_is_continuous_across_the_safety_fold` | tick by tick past 2^32; the fold is invisible and counted exactly once |
| `elapsed_is_unbounded_without_any_call_from_the_user` | 12 billion ticks — three full wraps, with nothing for a user to forget |
| `a_long_interval_polled_late_still_fires` | polled past both the interval and the fold threshold, it still fires — the fold is invisible to `isDue` |
| `slow_handler_does_not_shift_cadence` | a 249 ms handler still fires at 250, 500, 750 — `isDue` advances *before* the handler |
| `handler_slower_than_interval_never_fires_twice_running` | a 300 ms handler self-limits instead of building a backlog |
| `long_stall_fires_once_not_once_per_missed_slot` | 1000 ms past due yields **one** firing, not four |
| `has_elapsed_does_not_advance_the_timer` | asking twice gives the same answer — what separates it from `isDue` |
| `fires_across_a_clock_wrap` | both sides of the boundary, 249 false and 250 true, spanning the ceiling |
| `a_16_bit_tick_measures_across_its_own_wrap` | 65000 to 100 on a declared 16-bit tick is 636, not 4294902396 |
| `elapsed_is_unbounded_on_a_narrow_tick` | the fold follows the range: 10,000 reads of an 8-bit tick add up to exactly 1,000,000 |
| `an_elapsed_read_cannot_swallow_a_due_firing` | `elapsed()` at 120 and 128 on an 8-bit tick, then `isDue` at 128 fires; it used to wait until 228 |
| `due_every_beyond_a_narrow_range_fires` | a 300-tick interval on an 8-bit tick fires 333 times in 100,000 ticks; it used to fire none |

The tests were checked by mutation rather than assumed. The implementation was deliberately broken in the way a test exists to catch, and the suite was run against it:

| Broken how | Tests that fail |
| --- | --- |
| advancing by one interval instead of to now | `long_stall_fires_once_not_once_per_missed_slot` only |
| a reset that keeps the last firing | `reset_restarts_the_due_interval` only |
| `isDue` not recording its firing | 7, starting with `does_not_fire_again_immediately` |
| folding at a fixed 2^31 instead of half the tick's range | 4, including `elapsed_is_unbounded_on_a_narrow_tick` |

One mutant survives on purpose. Removing `remaining()`'s overdue check changes nothing, because the clamped subtraction then saturates to the same 0. The check stays anyway, because the clamps are a backstop, not the logic.

## Licence

MIT, matching the `elapsedMillis` original whose design this follows.
