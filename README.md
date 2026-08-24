# elapsedTime

A C-Next port of the ergonomics behind Teensy's `elapsedMillis`, for **any MCU** and **any tick source**.

> **Status: design settled, implementation blocked.** No code yet — see
> [Blocked on c-next](#blocked-on-c-next) below.

## What it is

`elapsedMillis` is one of the most useful things in the Teensy core: declare a timer,
read it, reset it.

```cpp
elapsedMillis elapsed250;
if (elapsed250 > 250) { elapsed250 = 0; fireThe250msThings(); }
```

Its entire ergonomic comes from C++ operator overloading — 14 of its 16 members are
operators. C-Next has no classes and no operator overloading, so the syntax cannot
port. **The capability can.**

```cnx
ElapsedTime.Config elapsed250 <- { startedAt: 0, tick: millis };

u32 e <- ElapsedTime.value(elapsed250);
if (e >= 250) {
    ElapsedTime.reset(elapsed250);
    fireThe250msThings();
}
```

## Why it is not `elapsedMillis`

Teensy's version hard-codes `millis()`. This one takes the clock as a parameter, so it
runs anywhere with a free-running `u32` counter:

| Platform | Tick source |
| --- | --- |
| Arduino (AVR, ESP32, STM32, RP2040, SAMD, Teensy) | `millis()` / `micros()` |
| STM32 HAL | `HAL_GetTick()` — same type, same units, same wrap |
| FreeRTOS | `xTaskGetTickCount()` via a `u32` wrapper |
| Zephyr | `k_cycle_get_32()` |
| Cortex-M3/M4/M7 | `DWT->CYCCNT` |
| Host tests | a fake clock you advance by hand |

Because the clock is per-instance, milliseconds and microseconds coexist in one
program — which also collapses `elapsedMillis` and `elapsedMicros` into a single type
rather than two byte-for-byte duplicates.

## Design

```cnx
// The callback type. ADR-029: a function definition creates both a function and a
// type, and a field of that type initialises to it -- so the clock is never null.
u32 tickSource() {
    return 0;
}

scope ElapsedTime {
    public struct Config {
        u32 startedAt;      // tick value at the last reset
        tickSource tick;    // the clock, supplied by the caller
    }

    public u32 value(const Config timer);
    public void reset(Config timer);
}
```

**Elapsed time is computed, never stored.** `value()` returns `tick() - startedAt` on
every read, so a timer cannot go stale and needs no per-loop update step. One `u32` of
state plus the clock pointer.

**Wrap handling is explicit.** `millis()` and every equivalent wrap every 49.7 days.
Teensy's version stays correct across that only because C's unsigned subtraction wraps;
C-Next defaults to clamp arithmetic, which saturates instead and would stall every timer
for 49.7 days. So the wrap is one visible branch rather than an invisible property of the
arithmetic:

```cnx
if (now >= startedAt) { return now - startedAt; }
return (4294967295 - startedAt) + now + 1;
```

That expression cannot overflow: the second branch runs only when `now < startedAt`, so
its result is at most `4294967295` exactly.

## Blocked on c-next

The design transpiles, and the C it generates is what you would hand-write:

```c
uint32_t ElapsedTime__value(const ElapsedTime__Config* timer);
void     ElapsedTime__reset(ElapsedTime__Config* timer);
ElapsedTime__Config elapsed250 = { .startedAt = 0U, .tick = millis };
```

But a function-as-type field inside a scope-nested struct does not emit its
function-pointer typedef, so the output does not compile:

- [jlaustill/c-next#1200](https://github.com/jlaustill/c-next/issues/1200) — function-as-type as a scope field, and as a field of a scope-nested struct
- [jlaustill/c-next#1201](https://github.com/jlaustill/c-next/issues/1201) — function-as-type as a function parameter

Implementation waits on those rather than working around them.

## Licence

MIT, matching the `elapsedMillis` original whose design this follows.
