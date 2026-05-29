#include "pico/stdlib.h"
#include "hardware/pwm.h"

// GP15 — change if you use a different pin
#define SERVO_PIN 15

// MG996R continuous rotation servo expects:
// 50Hz PWM = 20ms period
// ~1500us = STOP (neutral)
// ~1300us = one direction (speed depends on distance from 1500)
// ~1700us = other direction

// Pico PWM: wrap at 25000, clkdiv 100 → 1 count = 4us at 125MHz
// Actually we'll use: clkdiv=64, wrap=39062 → ~50Hz
// Easier: use microsecond helper below

#define PWM_FREQ_HZ     50
#define CLKDIV          64.0f
#define WRAP            (uint16_t)(125000000.0f / CLKDIV / PWM_FREQ_HZ - 1)

// Convert microseconds to PWM level
static inline uint16_t us_to_level(uint32_t us) {
    // period = 1/50Hz = 20000us, level = us/20000 * (WRAP+1)
    return (uint16_t)((uint32_t)(WRAP + 1) * us / 20000);
}

void servo_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, CLKDIV);
    pwm_config_set_wrap(&cfg, WRAP);
    pwm_init(slice, &cfg, true);
}

void servo_write_us(uint pin, uint32_t pulse_us) {
    uint slice = pwm_gpio_to_slice_num(pin);
    uint chan  = pwm_gpio_to_channel(pin);
    pwm_set_chan_level(slice, chan, us_to_level(pulse_us));
}

int main() {
    stdio_init_all();
    servo_init(SERVO_PIN);

    // --- After your physical mod, find the exact stop point first ---
    // Send 1500us and fine-tune until motor is completely still
    // then test 1300 / 1700 for speed/direction

    while (true) {
        // Spin forward (adjust 1300 → closer to 1500 = slower)
        servo_write_us(SERVO_PIN, 1300);
        sleep_ms(2000);

        // Stop
        servo_write_us(SERVO_PIN, 1500);
        sleep_ms(1000);

        // Spin reverse (adjust 1700 → closer to 1500 = slower)
        servo_write_us(SERVO_PIN, 1700);
        sleep_ms(2000);

        // Stop
        servo_write_us(SERVO_PIN, 1500);
        sleep_ms(1000);
    }
}