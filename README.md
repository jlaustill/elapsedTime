# elapsedTime

Portable, tick-agnostic elapsed-time timers for [C-Next](https://github.com/jlaustill/c-next) — the ergonomics behind Teensy's `elapsedMillis`, for **any MCU**, **any tick source**, and with **no 49.7-day ceiling**.

> **Status: design settled, implementation blocked.** No code yet — see [Blocked on c-next](#blocked-on-c-next).

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
ElapsedTime.Config elapsed250 <- { total: 0, startedAtTick: 0, rollOverAt: 0, tick: millis };

void loop() {
    u32 e <- ElapsedTime.timeSince(elapsed250);
    if (e >= 250) {
        ElapsedTime.reset(elapsed250);
        fireThe250msThings();
    }
}
```

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
        tickSource tick;        // the clock
    }

    public u32 timeSince(const Config timer);   // wrap-safe ticks since the last fold/reset
    public u64 value(const Config timer);       // total + timeSince
    public void handleOverflow(Config timer);   // fold timeSince into total
    public void seed(Config timer, u32 now);    // set the origin explicitly
    public void reset(Config timer);            // seed(timer, timer.tick())
}
```

14 bytes per timer on AVR.

## Design notes

**Elapsed time is computed, never stored.** `timeSince()` returns `tick() - startedAtTick` on every read, so a timer cannot go stale and needs no per-loop update step.

**Wrap handling is one visible branch**, not a property of the arithmetic. Every `u32` tick source wraps — 49.7 days at milliseconds, 71.6 minutes at microseconds. Teensy's version stays correct across that only because C's unsigned subtraction wraps; C-Next defaults to clamp arithmetic, which saturates instead and would stall every timer for a full wrap period. So it is explicit:

```cnx
if (now >= startedAtTick) { return now - startedAtTick; }
return (4294967295 - startedAtTick) + now + 1;
```

That expression cannot overflow: the second branch runs only when `now < startedAtTick`, so its result is at most `4294967295` exactly.

**Overflow handling is opt-in and costs nothing when unused.** `handleOverflow` folds the un-drained interval into `total` and restarts the interval from the same tick reading — so the intervals abut exactly and **no time is lost at the boundary**. Never call it and `total` stays 0, making `value()` identical to `timeSince()` with the ordinary 49.7-day ceiling. No flag, no second type: the feature is a call you make or don't.

**Each timer is serviced independently.** A timer that matters can be folded every second and one that doesn't every minute, and a module in another file can service its own timer from its own update function. Nothing is shared, so nothing coordinates — and a module that forgets only affects its own timer.

**`rollOverAt` is a tuning knob, not a requirement.** At `0` every `handleOverflow` call folds. Raise it and calls that aren't due become a compare and a branch instead of a 64-bit add, which makes the function cheap to call from a fast loop.

**`seed()` exists for exact origins.** C-Next has no constructors (ADR-005) and a global's initializer must be a compile-time constant, so a timer cannot capture `millis()` where it is declared. `seed()` sets the origin explicitly — and seeding several timers from one clock reading aligns them exactly, rather than letting them drift by the ticks between separate `reset()` calls.

## Blocked on c-next

The design transpiles, and the C it generates is what you would hand-write:

```c
uint32_t ElapsedTime__timeSince(const ElapsedTime__Config* timer);
void     ElapsedTime__reset(ElapsedTime__Config* timer);
ElapsedTime__Config elapsed250 = { .total = 0ULL, .startedAtTick = 0U, .tick = millis };
```

But a function-as-type field inside a scope-nested struct does not emit its function-pointer typedef, so the output does not compile:

- [jlaustill/c-next#1200](https://github.com/jlaustill/c-next/issues/1200) — function-as-type as a scope field, and as a field of a scope-nested struct **← the blocker**
- [jlaustill/c-next#1201](https://github.com/jlaustill/c-next/issues/1201) — function-as-type as a function parameter

Found while designing this library, not blocking it:

- [jlaustill/c-next#1202](https://github.com/jlaustill/c-next/issues/1202) — non-constant initializer on a global struct emits invalid C. `seed()` is the answer to this, so it shapes the API rather than blocking it.
- [jlaustill/c-next#1203](https://github.com/jlaustill/c-next/issues/1203) — ADR-006 not enforced; a non-addressable argument to a mutating parameter silently discards the mutation.

Implementation waits on the blocker rather than working around it.

## Licence

MIT, matching the `elapsedMillis` original whose design this follows.
