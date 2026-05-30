#include "pico/stdlib.h"
#include "hardware/pwm.h"

#define SERVO_PIN 16

#define PWM_FREQ_HZ 50
#define CLKDIV 64.0f
#define WRAP (uint16_t)(125000000.0f / CLKDIV / PWM_FREQ_HZ - 1)

static inline uint16_t us_to_level(uint32_t us) {
    return (uint16_t)((WRAP + 1) * us / 20000);
}

void servo_write_us(uint pin, uint32_t us) {
    uint slice = pwm_gpio_to_slice_num(pin);
    uint chan = pwm_gpio_to_channel(pin);

    pwm_set_chan_level(slice, chan, us_to_level(us));
}

int main() {

    stdio_init_all();

    // Set GPIO16 to PWM mode
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);

    // Get PWM slice
    uint slice = pwm_gpio_to_slice_num(SERVO_PIN);

    // Configure PWM
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, CLKDIV);
    pwm_config_set_wrap(&cfg, WRAP);

    pwm_init(slice, &cfg, true);

    // Wait a moment
    sleep_ms(2000);

    // FULL SPEED
    // Try 1300 first
    servo_write_us(SERVO_PIN, 1510);

    while (true) {
        tight_loop_contents();
    }
}