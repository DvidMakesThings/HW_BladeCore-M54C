/*
 * Heartbeat.c - BladeCore-M54C Heartbeat LED (PWM fade in/out)
 *
 * Smoothly fades the onboard blue heartbeat LED on GPIO36 using
 * hardware PWM.  Brightness ramps linearly from off to 100% duty
 * and back for a clean, visible breathing effect.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "CONFIG.h"

/* PWM wrap value -- higher gives finer brightness resolution */
#define PWM_WRAP 65535
#define FADE_STEPS 128

static void heartbeat_pwm_init(void)
{
    gpio_set_function(PIN_HEARTBEAT, GPIO_FUNC_PWM);

    uint slice = pwm_gpio_to_slice_num(PIN_HEARTBEAT);
    uint channel = pwm_gpio_to_channel(PIN_HEARTBEAT);

    pwm_config cfg = pwm_get_default_config();

    float divider = (float)clock_get_hz(clk_sys) /
                    ((float)HEARTBEAT_PWM_FREQ_HZ * (PWM_WRAP + 1));
    if (divider < 1.0f)
        divider = 1.0f;

    pwm_config_set_clkdiv(&cfg, divider);
    pwm_config_set_wrap(&cfg, PWM_WRAP);

    pwm_init(slice, &cfg, true);
    pwm_set_chan_level(slice, channel, 0);
}

static inline void heartbeat_set_brightness(uint16_t level)
{
    uint slice = pwm_gpio_to_slice_num(PIN_HEARTBEAT);
    uint channel = pwm_gpio_to_channel(PIN_HEARTBEAT);
    pwm_set_chan_level(slice, channel, level);
}

int main(void)
{
    stdio_init_all();
    heartbeat_pwm_init();

    /*
     * Simple fade in / fade out:
     *   step 0 -> 127  = off to full brightness  (fade in)
     *   step 127 -> 0  = full brightness to off  (fade out)
     *   Cycle time ~ 128 * 2 * 8 ms = ~2.0 s
     */
    int step = 0;
    int direction = 1;

    while (true)
    {
        /* Linear ramp: step/127 * PWM_WRAP */
        uint32_t brightness = ((uint32_t)step * PWM_WRAP) / (FADE_STEPS - 1);
        heartbeat_set_brightness((uint16_t)brightness);

        sleep_ms(HEARTBEAT_FADE_STEP_MS);

        step += direction;
        if (step >= FADE_STEPS)
        {
            step = FADE_STEPS - 1;
            direction = -1;
        }
        else if (step < 0)
        {
            step = 0;
            direction = 1;
        }
    }
}
