# Platform survey — gap list

Each canonical platform example, attempted in **both** C-Next and C/C++, built
against elapsedTime as a real `lib_deps` consumer. This file records what fought
back. **No fixes in this pass** — the point is a list to work through before
v1.0.0 is cut.

Toolchain note: c-next must be built from `main`; the released `cnext` 0.3.0 on
PATH cannot transpile the library at all. The `main` build reproduces the
committed `.c`/`.h` byte for byte, which is what these examples were built on.

## Build matrix

| Platform | Framework | Canonical example | C-Next | C/C++ |
| --- | --- | --- | --- | --- |
| Arduino AVR (Uno) | arduino | `BlinkWithoutDelay` | **PASS** | **PASS** |
| ESP32 | arduino | `BlinkWithoutDelay` | **FAIL** — G1, G2 | **FAIL** — G1 |
| ESP32 | espidf | `esp_timer` periodic | **PASS** (G4 friction) | **PASS** |
| STM32 Nucleo F401RE | stm32cube | CubeMX blink on `HAL_GetTick` | **PASS** (G4 friction) | **PASS** |
| STM32 Nucleo F401RE | arduino | `BlinkWithoutDelay` | **PASS** | not attempted |
| Teensy 4.0 | arduino | `BlinkWithoutDelay` vs `elapsedMillis` | **PASS** | **PASS** |

Where both variants built, they are the same size to the byte — AVR 30 B RAM /
1072 B flash, Teensy 8116 B code. The abstraction costs nothing over the
hand-written `previousMillis` original, in either language.

Not attempted: Raspberry Pi Pico (SDK), Zephyr, bare FreeRTOS. Each would add
another instance of a gap class already characterised below (G4 for header
resolution, G5 for tick width) rather than a new one, so they were skipped
rather than padded in. Pico is still worth doing for real once G5 is settled,
because `get_absolute_time()` returns a struct and `time_us_64()` a native u64.

---

## Status — paused pending c-next v0.4.1 (as of 2026-09-20)

Investigating G1 traced its root cause upstream. Three c-next issues were filed
from this survey, all milestoned **V0.4.1**:

| Issue | What it covers |
| --- | --- |
| [c-next#1619](https://github.com/jlaustill/c-next/issues/1619) | **G1's root cause.** E0879 is silent for a scope-declared callback type across an include — exactly this library's shape — and for any function from a C header. C-Next should have rejected `tick: millis` at transpile time on every platform. |
| [c-next#1618](https://github.com/jlaustill/c-next/issues/1618) | ADR-024 conversions enforced per-position: 6 of 10 positions leak, and every position leaks from a C source. Found while checking whether the ESP-IDF `now[0, 32]` adapter was actually required — it is correct, but nothing would have forced it. |
| [c-next#1620](https://github.com/jlaustill/c-next/issues/1620) | Why neither surfaced: adr-matrix occupancy is per-ADR rather than per-diagnostic, so one rule greens a cell for every code an ADR declares. |

They join existing cards [#1523](https://github.com/jlaustill/c-next/issues/1523)
and [#1524](https://github.com/jlaustill/c-next/issues/1524), which already
identified the missing interop axis from the tooling side; the repros here are
the concrete failing cells those asked for. All five are cross-linked.

**Do not work G1 until v0.4.1 ships.** Whether `tickSource` keeps its `uint32_t`
spelling is a genuine design choice, but it should be made against a transpiler
that reports the mismatch rather than one that passes it to gcc. The wrapper
workaround is verified to clear G1 in both languages on real ESP32 builds
(C++ alone; C-Next needs the G2 shim as well) — it is not committed, because
this branch is a record of gaps rather than of workarounds.

G2–G6 are independent of the upstream fix and can be worked at any time.

---

## G1 — `tick: millis` does not type-check on ESP32

**Severity: blocking.** This is the README's headline usage, and it fails on one
of the three boards with hardware on hand, in **both** languages.

`tickSource` generates `typedef uint32_t (*ElapsedTime__tickSource_fp)(void);`,
while every Arduino core declares `unsigned long millis()`. Whether those agree
is target-dependent:

| Target | `__UINT32_TYPE__` | `tick: millis` |
| --- | --- | --- |
| AVR | `long unsigned int` | compiles |
| ARM (Teensy, STM32) | `long unsigned int` | compiles |
| **Xtensa (ESP32)** | `unsigned int` | **fails** |

So AVR, ARM and Teensy pass by coincidence, not by design — the library's
contract is with a fixed-width type, the Arduino contract is with
`unsigned long`, and those happen to be the same type on some targets.

C (transpiled from `.cnx`) — a warning, which would have linked:

    src/main.c:20:62: warning: initialization of 'uint32_t (*)(void)'
    {aka 'unsigned int (*)(void)'} from incompatible pointer type
    'long unsigned int (*)()' [-Wincompatible-pointer-types]

C++ — a hard error:

    src/main.cpp:7:75: error: invalid conversion from 'long unsigned int (*)()'
    to 'ElapsedTime__tickSource_fp' {aka 'unsigned int (*)()'} [-fpermissive]

For the fix pass: a wrapper resolves it but costs the one-line declaration that
is the library's whole selling point, on the most popular 32-bit target.

## G2 — a `.cnx` main cannot supply `setup`/`loop` on ESP32

**Severity: blocking for the C-Next variant, ESP32 only.** Verified by reading
each core's header, not inferred:

| Core | `setup`/`loop` declared | Inside `extern "C"`? | Links from C |
| --- | --- | --- | --- |
| AVR | `Arduino.h` 154–155 | yes (block 35–227) | yes |
| STM32duino | `api/Common.h` 139–140 | yes (block 24–143) | yes |
| Teensy | — | yes (empirically) | yes |
| **ESP32** | `Arduino.h` 149–150 | **no** (block is 168–182) | **no** |

ESP32 declares them under a bare `#ifdef __cplusplus`, *before* its `extern "C"`
block, so they carry C++ linkage and the generated C cannot satisfy them:

    undefined reference to `setup()'
    undefined reference to `loop()'

An Arduino-core inconsistency rather than a library defect — but the library
ships the examples, so it needs an answer, most likely a documented C++ shim
forwarding `setup`/`loop` into the `.cnx`.

## G3 — transpiling a consumer rewrites the library's header to the wrong place

**Severity: papercut.** An example's `cnext.config.json` governs where the
*dependency's* generated header lands. Transpiling any example regenerates
elapsedTime too and drops `ElapsedTime.h` into the library's `src/`, beside the
canonical copy in `include/`:

    Generated 4 output files:
      .../elapsedTime/src/ElapsedTime.c
      .../examples/<ex>/cnx/src/main.c
      .../elapsedTime/src/ElapsedTime.h     <-- stray; canonical is include/
      .../examples/<ex>/cnx/src/main.h

`git status` then shows `?? src/ElapsedTime.h` after building any example. Every
example build in this survey needed a manual `rm` afterwards.

## G4 — C-Next resolves Arduino and nothing else

**Severity: high friction, affects every non-Arduino platform.**

C-Next carries a hardcoded table (`src/TRANSPILE/1-Analyze/StdlibFunctions.ts`)
mapping known C functions to headers. It covers the C standard library plus an
"Arduino framework" block (`millis`, `micros`, `pinMode`, `digitalWrite`, …).
Proof it is a built-in table rather than header parsing: with `Arduino.h` *not*
on the include path at all, `millis()` resolves and a made-up name does not.

Every other ecosystem — ESP-IDF, STM32 HAL, Pico SDK, Zephyr — is absent, so a
bare call fails:

    error[E0422]: function 'esp_timer_get_time' called before definition
    error[E0422]: 'esp_timer_get_time' is not declared in any included header

The escape hatch works but is heavy. C-Next runs a **real C preprocessor** over
included headers, so `cnext.config.json` must reproduce the build's include
path, which the platform's own build system normally computes:

```json
"include": [
    ".../components/esp_timer/include/",
    ".../components/freertos/FreeRTOS-Kernel/include/"
]
```

Even then, headers with deep chains fail to preprocess and fall back to raw
scanning — `FreeRTOS.h` could not find `FreeRTOSConfig.h`, and `vTaskDelay`
stayed unresolvable, so the ESP-IDF example had to drop it.

**Sub-finding — the include paths are machine-specific.** `cnext.config.json`
takes no variable expansion, so the ESP-IDF and STM32 examples in this tree
carry hardcoded absolute paths into `~/.platformio/packages/...`:

    "include": ["/home/linux/.platformio/packages/framework-stm32cubef4/..."]

They build here and nowhere else. Shipping a `.cnx` example for any non-Arduino
platform needs an answer to this — a variable, a PlatformIO-provided path, or
generating the config in a build hook.

**Sub-finding, worth its own look:** `cnext.config.json` accepts a `defines` map
(`ITranspilerConfig.defines`), but setting `{"STM32F401xE": true,
"USE_HAL_DRIVER": true}` did **not** suppress the HAL's device-select `#error`.
Either the key is not honoured on this path or more is needed; it transpiled
anyway via raw-content fallback. Not chased further in this pass.

## G5 — a tick narrower than 32 bits cannot be adapted, and fails silently

**Severity: the most serious design gap found.** The README says the library
takes "any `u32` tick" and "anything with a free-running `u32` counter". Any
source that wraps at less than 2^32 breaks it, and the natural adapter —
widening to u32 — is catastrophically and silently wrong.

`sinceTick`'s wrap correction assumes the counter wraps at exactly 2^32:

    return (4294967295 - base) + now + 1;

Feed it a counter that wraps at 2^16 and that branch fabricates an enormous
interval. Minimal reproduction (a u16 counter widened to u32, 65000 → 100,
i.e. 636 real ticks):

    actual elapsed:   636
    library reports:  4294902396
    WRONG

This is not hypothetical. FreeRTOS with `configUSE_16_BIT_TICKS=1` makes
`TickType_t` a `uint16_t`. More common still, raw hardware counters are
routinely narrower than 32 bits — AVR Timer1 is 16-bit, ARM SysTick is 24-bit —
and the README explicitly invites feeding the library "a counter", including
`DWT->CYCCNT`-style sources.

For the fix pass: the library either needs to know the tick's modulus, or needs
to reject/document narrower sources loudly. Silent 6.7-million-fold error is the
worst possible behaviour.

## G6 — "unbounded, automatic" carries an undocumented polling requirement

**Severity: documentation gap with a real failure behind it.** The README
promises elapsed time past 49.7 days needs "nothing extra", that folding is
"internal and automatic", and that there is nothing "for a user to forget".

There is: **the caller must poll at least once per 2^32 ticks.** `sinceTick` can
only measure distances below one wrap, so a longer gap between calls loses a
whole wrap undetectably. Reproduction — seed at 0, let 5,000,000,000 ms pass,
then read:

    real time elapsed: 5000000000
    library reports:   705032704
    shortfall:         4294967296

The suite's `elapsed_is_unbounded_without_any_call_from_the_user` passes because
it advances the clock in small steps; it does not cover a single jump wider than
a wrap. At microseconds the requirement is "poll every 71.6 minutes", which a
sleepy low-power design can easily miss — and `remaining()` exists specifically
to help size long sleeps.

---

## Not gaps — C-Next ergonomics worth folding into the AI reference

Hit while writing the `.cnx` examples. All are the transpiler steering toward
MISRA-clean code, and all were resolved by following its own help text:

- `while (true)` → `error[E0701]` (MISRA 14.4, condition must be a comparison),
  then `error[E0707]` on the workaround, with the right answer in the help:
  a dedicated `forever { }` construct, which emits `for (;;)`.
- A discarded return value → `error[E0708]` (ADR-070); write `(void) printf(...)`.
- C functions sometimes need an explicit `global.` prefix where a bare call is
  not resolvable.
- `i64` → `u32` truncation via bit extraction, `now[0, 32]`, generates exactly
  `(uint32_t)((now) & 0xFFFFFFFFULL)` — clean, and the right answer for the
  ESP-IDF adapter.
