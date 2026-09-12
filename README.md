# elapsedTime

Portable, tick-agnostic elapsed-time timers for [C-Next](https://github.com/jlaustill/c-next) — the ergonomics behind Teensy's `elapsedMillis`, for **any MCU**, **any tick source**, and with **no 49.7-day ceiling**.

> **Status: implemented, 22/22 tests passing.** Requires a C-Next build from
> `main`; the released `0.3.0` cannot transpile it — see
> [Requirements](#requirements).

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
ElapsedTime.Timer elapsed250 <- { dueEvery: 250, tick: millis };

void loop() {
    bool due <- ElapsedTime.isDue(elapsed250);
    if (due = true) {
        fireThe250msThings();
    }
}
```

That declaration is the whole setup. There is no constructor, no `setup()` step and no periodic service call — **if a user can forget it, the library does not need it.**

`isDue` is the whole pattern in one call: it reports whether the interval has elapsed and advances the timer if it has, so there is no reset to forget, and the interval lives with the timer rather than being repeated at every call site.

(Two lines rather than one because C-Next forbids a function call inside an `if` condition — MISRA C:2012 Rule 13.5, `E0702`.)

Uptime past 49.7 days needs nothing extra — folding is internal and automatic:

```cnx
u64 uptime <- ElapsedTime.elapsed(elapsedSystemTime);
```

## Any tick source

The clock is a caller-supplied callback, so the library never names a platform:

| Platform | Tick source |
| --- | --- |
| Arduino (AVR, ESP32, STM32, RP2040, SAMD, Teensy) | `millis()` / `micros()` |
| STM32 HAL | `HAL_GetTick()` — same type, same units, same wrap |
| FreeRTOS | `xTaskGetTickCount()` via a `u32` wrapper |
| Zephyr | `k_cycle_get_32()` |
| Cortex-M3/M4/M7 | `DWT->CYCCNT` |
| Host tests | a fake clock you advance by hand |

It also means the library is not embedded-specific at all. Nothing in it names a framework, so it builds and runs on a host — the entire test suite runs natively against a fake clock, no board and no cross-compiler involved. A desktop application, a server, a game loop or a wasm build can use it exactly as an MCU does.

### The tick need not be a clock

Nothing in the library assumes time. `sinceTick` needs only a monotonically increasing `u32`, so a counter works exactly as well as a clock — feed it a value your own code increments and `dueEvery: 100` means *every hundredth execution*, while `elapsed()` becomes a total execution count, unbounded past 2³² exactly as time is.

```cnx
u32 framesSeen <- 0;
u32 frameTick() { return framesSeen; }      // a pure READ

ElapsedTime.Timer everyHundredthFrame <- { dueEvery: 100, tick: frameTick };
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
        u64 total;           // internal -- folded elapsed
        u32 startedAtTick;   // internal -- origin of the current interval
        bool started;        // internal -- false until the first touch
    }

    public bool isDue(Timer timer);                  // due? if so, advance and say true
    public u64  elapsed(Timer timer);                // total elapsed, unbounded
    public bool hasElapsed(Timer timer, u32 ticks);  // non-advancing threshold test
    public u32  remaining(Timer timer);              // ticks until due
    public void reset(Timer timer);                  // origin <- tick()
    public void resetTo(Timer timer, u32 now);       // origin <- now
}
```

Only the first two fields are ever written at a call site; the rest zero-fill (C99 6.7.9p21). Field order is deliberate — the struct reads in the order it is typed. It is three bytes smaller than the proof of concept's: `started` adds one, deleting `rollOverAt` removes four.

## Design notes

**Elapsed time is computed, never stored.** Every read derives `tick() - startedAtTick` afresh, so a timer cannot go stale and needs no per-loop update step.

**A timer seeds itself on first touch.** C-Next has no constructors (ADR-005) and a global's initializer must be a compile-time constant, so a timer cannot capture `millis()` where it is declared. A timer left at `startedAtTick: 0` while the clock already reads 40,000 would fire immediately and report 40 seconds that never elapsed. The internal `started` flag is false until the first call, which seeds the origin from the clock and reports not-yet; the first firing lands one full interval later. Seeding lives in one private function, so `isDue` and `elapsed` cannot disagree about what a fresh timer means.

**Wrap handling is one visible branch**, not a property of the arithmetic. Every `u32` tick source wraps — 49.7 days at milliseconds, 71.6 minutes at microseconds. Teensy's version stays correct across that only because C's unsigned subtraction wraps; C-Next defaults to clamp arithmetic, which saturates instead and would stall every timer for a full wrap period. So it is explicit:

```cnx
if (now >= startedAtTick) { return now - startedAtTick; }
return (4294967295 - startedAtTick) + now + 1;
```

That expression cannot overflow: the second branch runs only when `now < startedAtTick`, so its result is at most `4294967295` exactly.

**`isDue` advances before the handler runs, and that ordering is the design.** It reads the clock once, and if the interval has elapsed it folds and advances *before* returning `true` — so the caller's handler executes on already-advanced state and its duration cannot push the next firing:

```
t=250   isDue -> fires, startedAtTick <- 250
        handler runs 249 ms
t=499   isDue -> since 249 -> false
t=500   isDue -> since 250 -> fires
```

Firings land at 250, 500, 750. The same two statements in the other order drift by the handler's duration every time, which is why the sequencing is commented in the implementation rather than left looking arbitrary.

**It advances to *now*, not by exactly one interval.** Advancing by the interval is drift-free against late polling, but when a handler outruns its interval it either silently skips a slot or fires repeatedly to catch up. Advancing to now does neither: it never skips, never fires twice in a row, and when the handler is genuinely slower than the interval it self-limits to the handler's real rate instead of building a backlog. The only residual drift is polling latency, which is microseconds when `isDue` runs each loop.

**`dueEvery: 0` means never due**, not always due. A zero-initialised timer firing on every call would be a trap, and `0 >= 0` is true for unsigned values.

**`isDue` folds rather than resets**, so `elapsed()` stays a true cumulative on a polled timer. `reset()` would zero `total` and leave `elapsed()` permanently near zero on anything periodic.

**Overflow handling is automatic and private.** The proof of concept made the caller run `handleOverflow()` periodically to push past the 49.7-day ceiling — a step a user can forget, and forgetting it was silent. Folding now happens inside the accessors at a fixed **2^31** threshold: 24.8 days of margin at milliseconds, 35.8 minutes at microseconds, for one `u64` add per 2^31 ticks. It is also the cheapest threshold to test — `since >= 0x80000000` is "is bit 31 set", a single high-byte test on AVR rather than a four-byte comparison. The fold restarts the interval from the same tick reading, so the intervals abut exactly and **no time is lost at the boundary**.

**`isDue` deliberately omits that safety fold, and the paths must not be unified.** A working periodic timer folds on every firing and never approaches 2^31, so the safety fold would buy nothing there — and it would actively *break* long intervals: `since` would fold to zero while still short of `dueEvery`, and the timer would never fire at all. A test pins this by polling a long interval late, past both the interval and the threshold; polling on time cannot detect the difference.

**`hasElapsed` runs on the `u64`.** It tests total elapsed against a threshold without advancing the timer, which serves one-shots that have no `dueEvery`. It is not a cheap `u32` path — "elapsed since reset" is `total + since`, inherently `u64`. The justification is reach: a `u64` of milliseconds tops out 584 million years away, so a threshold of a year is a legitimate call on hardware that will never reach its own ceiling.

**`remaining()` returns `u32max` when a timer is never due.** The use case is low-power scheduling, where a caller takes the *minimum* across several timers to size a sleep. `u32max` is the identity for minimum, so a never-due timer correctly does not constrain the sleep; returning 0 would make every never-due timer forbid sleeping entirely.

**`resetTo()` exists for exact origins.** `reset()` reads the clock; `resetTo()` takes the reading from you. One reading shared across several timers aligns them exactly, rather than letting them drift by the ticks between separate `reset()` calls.

**Every accessor mutates, so none can take `const`.** Folding is a write. That is honest rather than unfortunate: a timer is a stateful object, and `elapsedMillis` behaves the same way.

**Each timer is independent.** A module in another file owns its own timer, and nothing is shared, so nothing coordinates.

### Constraint

**`dueEvery` must stay below 2^31** — 24.8 days at milliseconds, 35.8 minutes at microseconds. Above that the safety fold and the due check fight, per the note above. `hasElapsed` is the right tool for longer intervals: it runs on the `u64` and tops out 584 million years away.

## Requirements

**C-Next from `main`.** The latest release, `0.3.0`, cannot transpile this library, and no release yet carries what it needs:

- [c-next#1207](https://github.com/jlaustill/c-next/pull/1207) (merged after `0.3.0` was cut) — a function-as-type field in a scope-nested struct emitted no function-pointer typedef, so the generated C did not compile.
- `tickSource` living *inside* the scope and referenced bare from `Timer` in that same scope. The header must emit `ElapsedTime__tickSource_fp`; earlier builds emitted a raw name that is not a type. Opened as [c-next#1281](https://github.com/jlaustill/c-next/pull/1281), which was closed unmerged after being blocked on [#1285](https://github.com/jlaustill/c-next/issues/1285); the fix reached `main` through that refactor instead.

The committed `.c`/`.h` were generated from `main`, and regenerating from `main` reproduces them byte for byte.

Two further defects found while designing this shaped the API rather than blocking it:

- [c-next#1202](https://github.com/jlaustill/c-next/issues/1202) — a global's initialiser must be a compile-time constant, so a timer cannot capture its clock where it is declared. This is why a timer seeds itself on first touch rather than through a builder.
- [c-next#1215](https://github.com/jlaustill/c-next/issues/1215) — a callback-typed *scope member* without an explicit initialiser is rejected as uninitialised. Not hit here: the clock is a struct field, not a scope member.

## Testing

```bash
pio test -e native
```

22 tests against a clock the suite drives by hand, so every assertion is exact rather than approximate. The cases that carry the design:

| Test | What it pins |
| --- | --- |
| `fresh_timer_against_a_running_clock_reports_no_elapsed_time` | a timer declared at t=40000 claims 0, not 40 seconds |
| `fresh_timer_fires_one_full_interval_after_first_touch` | lazy seeding — first firing at 40250, not immediately |
| `elapsed_is_continuous_across_the_safety_fold` | tick by tick past 2^32; the fold is invisible and counted exactly once |
| `elapsed_is_unbounded_without_any_call_from_the_user` | 12 billion ticks — three full wraps, with nothing for a user to forget |
| `a_long_interval_polled_late_still_fires` | `isDue` must **not** share the safety fold, or long intervals never fire |
| `slow_handler_does_not_shift_cadence` | a 249 ms handler still fires at 250, 500, 750 — `isDue` advances *before* the handler |
| `handler_slower_than_interval_never_fires_twice_running` | a 300 ms handler self-limits instead of building a backlog |
| `long_stall_fires_once_not_once_per_missed_slot` | 1000 ms past due yields **one** firing, not four |
| `has_elapsed_does_not_advance_the_timer` | asking twice gives the same answer — what separates it from `isDue` |
| `fires_across_a_clock_wrap` | both sides of the boundary, 249 false and 250 true, spanning the ceiling |

Two of these were verified by mutation rather than assumed. The implementation was deliberately broken in the way each test exists to catch — advancing by one interval instead of to now, and giving `isDue` the safety fold — and in each case exactly one test failed, the right one. The second mutation is what caught the *first* version of `a_long_interval_polled_late_still_fires`, which polled on time and so passed against both the correct and the broken implementation.

## Licence

MIT, matching the `elapsedMillis` original whose design this follows.
