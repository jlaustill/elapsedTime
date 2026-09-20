// ESP-IDF native -- periodic work driven by elapsedTime, from C++.
#include <stdio.h>
#include <esp_timer.h>
#include <ElapsedTime.h>

// The adapter: a 64-bit clock narrowed to the u32 the library takes.
static uint32_t microseconds() {
    return static_cast<uint32_t>(esp_timer_get_time());
}

static ElapsedTime__Timer everySecond = { .dueEvery = 1000000, .tick = microseconds };

extern "C" void app_main() {
    for (;;) {
        if (ElapsedTime__isDue(&everySecond)) {
            printf("tick, %llu us elapsed\n",
                   (unsigned long long)ElapsedTime__elapsed(&everySecond));
        }
    }
}
