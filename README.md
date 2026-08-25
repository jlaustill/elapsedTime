# elapsedTime

Portable, tick-agnostic elapsed-time timers for [C-Next](https://github.com/jlaustill/c-next) — the ergonomics behind Teensy's `elapsedMillis`, for **any MCU**, **any tick source**, and with **no 49.7-day ceiling**.

> **Status: implemented, 21/21 tests passing.** Requires a C-Next build carrying
> [c-next#1207](https://github.com/jlaustill/c-next/pull/1207) (merged, not yet
> released) — see [Requirements](#requirements).

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
| Ceiling | 49.7 days, silently wrong past it | unbounded, opt-in |
| Units | two duplicate classes | one type |

## Usage

```cnx
ElapsedTime.Config elapsed250 <- {
    total: 0, startedAtTick: 0, rollOverAt: 0, dueEvery: 250, tick: millis
};

void loop() {
    bool due <- ElapsedTime.isDue(elapsed250);
    if (due = true) {
        fireThe250msThings();
    }
}
```

`isDue` is the whole pattern in one call: it reports whether the interval has elapsed and advances the timer if it has, so there is no reset to forget, and the interval lives with the timer rather than being repeated at every call site.

(Two lines rather than one because C-Next forbids a function call inside an `if` condition — MISRA C:2012 Rule 13.5, `E0702`.)

Want it to outlive 49.7 days? Call `handleOverflow` somewhere that runs regularly — every second, every minute, whatever suits that timer:

```cnx
ElapsedTime.handleOverflow(elapsedSystemTime);
u64 uptime <- ElapsedTime.value(elapsedSystemTime);
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

Because the clock is **per-timer**, milliseconds and microseconds coexist in one program — which also collapses `elapsedMillis` and `elapsedMicros` into a single type instead of two byte-for-byte duplicates.

## API

```cnx
// ADR-029: a function definition creates both a function and a type, and a field of
// that type initialises to it -- so the clock is never null.
u32 tickSource() {
    return 0;
}

scope ElapsedTime {
    public struct Config {
        u64 total;              // elapsed folded in by previous handleOverflow calls
        u32 startedAtTick;      // origin of the current un-drained interval
        u32 rollOverAt;         // 0 = fold on every call; raise to skip the u64 math
        u32 dueEvery;           // isDue interval; 0 = never due
        tickSource tick;        // the clock
    }

    public bool isDue(Config timer);            // due? if so, advance and say true
    public u32 timeSince(const Config timer);   // wrap-safe ticks since the last fold
    public u64 value(const Config timer);       // total + timeSince
    public void handleOverflow(Config timer);   // fold timeSince into total
    public void seed(Config timer, u32 now);    // set the origin explicitly
    public void reset(Config timer);            // seed(timer, timer.tick())
}
```

18 bytes per timer on AVR.

## Design notes

**Elapsed time is computed, never stored.** `timeSince()` returns `tick() - startedAtTick` on every read, so a timer cannot go stale and needs no per-loop update step.

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
t=499   isDue -> timeSince 249 -> false
t=500   isDue -> timeSince 250 -> fires
```

Firings land at 250, 500, 750. The same two statements in the other order drift by the handler's duration every time, which is why the sequencing is commented in the implementation rather than left looking arbitrary.

**It advances to *now*, not by exactly one interval.** Advancing by the interval is drift-free against late polling, but when a handler outruns its interval it either silently skips a slot or fires repeatedly to catch up. Advancing to now does neither: it never skips, never fires twice in a row, and when the handler is genuinely slower than the interval it self-limits to the handler's real rate instead of building a backlog. The only residual drift is polling latency, which is microseconds when `isDue` runs each loop.

**`dueEvery: 0` means never due**, not always due. A zero-initialised timer firing on every call would be a trap, and `0 >= 0` is true for unsigned values.

**`isDue` folds rather than resets**, so `value()` stays a true cumulative on a polled timer. `reset()` would zero `total` and leave `value()` permanently near zero on anything periodic.

**Overflow handling is opt-in and costs nothing when unused.** `handleOverflow` folds the un-drained interval into `total` and restarts the interval from the same tick reading — so the intervals abut exactly and **no time is lost at the boundary**. Never call it and `total` stays 0, making `value()` identical to `timeSince()` with the ordinary 49.7-day ceiling. No flag, no second type: the feature is a call you make or don't.

**Each timer is serviced independently.** A timer that matters can be folded every second and one that doesn't every minute, and a module in another file can service its own timer from its own update function. Nothing is shared, so nothing coordinates — and a module that forgets only affects its own timer.

**`rollOverAt` is a tuning knob, not a requirement.** At `0` every `handleOverflow` call folds. Raise it and calls that aren't due become a compare and a branch instead of a 64-bit add, which makes the function cheap to call from a fast loop.

**`seed()` exists for exact origins.** C-Next has no constructors (ADR-005) and a global's initializer must be a compile-time constant, so a timer cannot capture `millis()` where it is declared. `seed()` sets the origin explicitly — and seeding several timers from one clock reading aligns them exactly, rather than letting them drift by the ticks between separate `reset()` calls.

## Requirements

C-Next from `main` carrying [c-next#1207](https://github.com/jlaustill/c-next/pull/1207), which is merged but **not yet in a released version**. Released `0.3.0` cannot transpile this library: a function-as-type field inside a scope-nested struct — `tickSource tick` inside `ElapsedTime.Config` — emitted no function-pointer typedef, so the generated C did not compile.

The committed `.c`/`.h` were generated by a post-#1207 build. Regenerating with `0.3.0` will produce output that does not compile; regenerating with `main` reproduces them.

Two further defects found while designing this shaped the API rather than blocking it:

- [c-next#1202](https://github.com/jlaustill/c-next/issues/1202) — a global's initialiser must be a compile-time constant, so a timer cannot capture its clock where it is declared. This is why `seed()` exists.
- [c-next#1215](https://github.com/jlaustill/c-next/issues/1215) — a callback-typed *scope member* without an explicit initialiser is rejected as uninitialised. Not hit here: the clock is a struct field, not a scope member.

## Testing

```bash
pio test -e native
```

21 tests against a clock the suite drives by hand, so every assertion is exact rather than approximate. The cases that carry the design:

| Test | What it pins |
| --- | --- |
| `slow_handler_does_not_shift_cadence` | a 249 ms handler still fires at 250, 500, 750 — `isDue` advances *before* the handler |
| `handler_slower_than_interval_never_fires_twice_running` | a 300 ms handler self-limits instead of building a backlog |
| `long_stall_fires_once_not_once_per_missed_slot` | 1000 ms past due yields **one** firing, not four |
| `repeated_folds_lose_no_time` | 1000 folds at an awkward 37-tick interval total exactly 37000 |
| `folding_past_the_u32_ceiling_is_unbounded` | 12 billion ticks — three full wraps past what a `u32` timer can express |
| `fires_across_a_clock_wrap` | both sides of the boundary, 249 false and 250 true, spanning the ceiling |
| `largest_measurable_interval_does_not_saturate` | `4294967295` exactly, no clamp |

## Licence

MIT, matching the `elapsedMillis` original whose design this follows.
