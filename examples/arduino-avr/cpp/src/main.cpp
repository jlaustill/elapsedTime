// Arduino AVR -- BlinkWithoutDelay, C++ consuming the generated header.
//
// The C-Next variant declares the timer as
//     ElapsedTime.Timer blinkTimer <- { dueEvery: 1000, tick: millis };
// This is the nearest C++ equivalent.

#include <Arduino.h>
#include <ElapsedTime.h>

static ElapsedTime__Timer blinkTimer = { .dueEvery = 1000, .tick = millis };

static bool ledOn = false;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    if (ElapsedTime__isDue(&blinkTimer)) {
        ledOn = !ledOn;
        digitalWrite(LED_BUILTIN, ledOn);
    }
}
