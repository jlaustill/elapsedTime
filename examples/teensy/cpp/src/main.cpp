// Teensy 4.0 -- BlinkWithoutDelay, C++ consuming the generated header.
//
// Side by side with the elapsedMillis idiom this library generalises:
//
//     elapsedMillis sinceBlink;              // interval lives at the call site
//     if (sinceBlink > 1000) {               // and must be repeated there
//         sinceBlink = 0;                    // a reset you can forget
//         toggle();
//     }

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
