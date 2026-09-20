// STM32 HAL -- the CubeMX blink, driven by elapsedTime instead of HAL_Delay.
//
// The generated CubeMX main() blocks: HAL_Delay(500) inside while(1). Polling
// HAL_GetTick() through a timer frees the loop to do other work.

#include "stm32f4xx_hal.h"
#include <ElapsedTime.h>

static ElapsedTime__Timer blinkTimer = { .dueEvery = 500, .tick = HAL_GetTick };

int main(void) {
    HAL_Init();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef led = {};
    led.Pin = GPIO_PIN_5;
    led.Mode = GPIO_MODE_OUTPUT_PP;
    led.Pull = GPIO_NOPULL;
    led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &led);

    for (;;) {
        if (ElapsedTime__isDue(&blinkTimer)) {
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        }
    }
}
