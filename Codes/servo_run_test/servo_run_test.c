#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

// GP16 for servo signal
#define SERVO_PIN 16

// Convert microseconds to PWM level
// 50Hz = 20ms period, clock divider = 64, wrap = 39062
// level = (us / 20000) * 39062
uint16_t us_to_level(uint32_t us) {
    return (uint16_t)((us * 39062) / 20000);
}

void servo_init() {
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(SERVO_PIN);
    
    // Set 50Hz frequency
    pwm_set_clkdiv(slice, 64.0f);
    pwm_set_wrap(slice, 39062);
    pwm_set_enabled(slice, true);
    
    printf("Servo initialized on GP%d\n", SERVO_PIN);
}

void servo_stop() {
    uint slice = pwm_gpio_to_slice_num(SERVO_PIN);
    uint channel = pwm_gpio_to_channel(SERVO_PIN);
    pwm_set_chan_level(slice, channel, us_to_level(1500));
    printf("Stopped (1500us)\n");
}

void servo_clockwise(int speed) {
    uint slice = pwm_gpio_to_slice_num(SERVO_PIN);
    uint channel = pwm_gpio_to_channel(SERVO_PIN);
    uint32_t us = 1500 - 45 - (speed * 710) / 100;
    pwm_set_chan_level(slice, channel, us_to_level(us));
    printf("CW at %d%% speed (%luus)\n", speed, us);
}

void servo_counter_clockwise(int speed) {
    uint slice = pwm_gpio_to_slice_num(SERVO_PIN);
    uint channel = pwm_gpio_to_channel(SERVO_PIN);
    uint32_t us = 1500 + 45 + (speed * 710) / 100;
    pwm_set_chan_level(slice, channel, us_to_level(us));
    printf("CCW at %d%% speed (%luus)\n", speed, us);
}

int main() {
    stdio_init_all();
    sleep_ms(2000); // Wait for USB serial to connect
    
    printf("=== FS90R Servo Test ===\n");
    
    servo_init();
    
    printf("Stopping...\n");
    servo_stop();
    sleep_ms(2000);
    
    printf("Clockwise 50%%...\n");
    servo_clockwise(50);
    sleep_ms(2000);
    
    printf("Stopping...\n");
    servo_stop();
    sleep_ms(1000);
    
    printf("Counter-clockwise 50%%...\n");
    servo_counter_clockwise(50);
    sleep_ms(2000);
    
    printf("Done.\n");
    servo_stop();
    
    return 0;
}
