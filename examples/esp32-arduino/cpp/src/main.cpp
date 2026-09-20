// ESP32 (Arduino core) -- BlinkWithoutDelay, C++ consuming the generated header.
#include <Arduino.h>
#include <ElapsedTime.h>

static const uint8_t LED_PIN = 2;

static ElapsedTime__Timer blinkTimer = { .dueEvery = 1000, .tick = millis };

static bool ledOn = false;

void setup() {
    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    if (ElapsedTime__isDue(&blinkTimer)) {
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn);
    }
}
