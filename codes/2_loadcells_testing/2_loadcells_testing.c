#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

// ---- Cell 0 ----
#define PIN_SCK0    18
#define PIN_DT0     19
// ---- Cell 1 ----
#define PIN_SCK1    16
#define PIN_DT1     17

#define IIR_ALPHA   0.08f
#define MAX_SAMPLES 5000

static int32_t  raw0_buf[MAX_SAMPLES];
static int32_t  raw1_buf[MAX_SAMPLES];
static float    filt0_buf[MAX_SAMPLES];
static float    filt1_buf[MAX_SAMPLES];
static uint32_t time_buf[MAX_SAMPLES];

static void hx711_init(uint sck, uint dt) {
    gpio_init(sck);
    gpio_set_dir(sck, GPIO_OUT);
    gpio_put(sck, 0);
    gpio_init(dt);
    gpio_set_dir(dt, GPIO_IN);
}

static int32_t hx711_read(uint sck, uint dt) {
    while (gpio_get(dt)) tight_loop_contents();

    uint32_t raw = 0;
    // SCK held HIGH > ~60 us powers down the HX711, so keep the
    // 25-pulse train atomic against USB interrupts.
    uint32_t irq = save_and_disable_interrupts();
    for (int i = 0; i < 24; i++) {
        gpio_put(sck, 1);
        busy_wait_us(1);
        raw = (raw << 1) | gpio_get(dt);
        gpio_put(sck, 0);
        busy_wait_us(1);
    }
    // 25th pulse: select channel A, gain 128
    gpio_put(sck, 1);
    busy_wait_us(1);
    gpio_put(sck, 0);
    restore_interrupts(irq);

    // sign-extend 24-bit two's complement
    if (raw & 0x800000) raw |= 0xFF000000;
    return (int32_t)raw;
}

int main() {
    stdio_init_all();
    hx711_init(PIN_SCK0, PIN_DT0);
    hx711_init(PIN_SCK1, PIN_DT1);

    // first reading uses wrong gain/channel, discard
    hx711_read(PIN_SCK0, PIN_DT0);
    hx711_read(PIN_SCK1, PIN_DT1);

    while (true) {
        int n;
        if (scanf("%d", &n) != 1) {
            getchar();          // drain offending char so scanf can recover
            continue;
        }
        if (n < 0) continue;

        // ---- streaming mode: send 0 ----
        if (n == 0) {
            printf("STREAM\n");
            float f0 = (float)hx711_read(PIN_SCK0, PIN_DT0);
            float f1 = (float)hx711_read(PIN_SCK1, PIN_DT1);
            uint32_t t0 = to_ms_since_boot(get_absolute_time());
            while (true) {
                int32_t r0 = hx711_read(PIN_SCK0, PIN_DT0);
                int32_t r1 = hx711_read(PIN_SCK1, PIN_DT1);
                f0 = IIR_ALPHA * (float)r0 + (1.0f - IIR_ALPHA) * f0;
                f1 = IIR_ALPHA * (float)r1 + (1.0f - IIR_ALPHA) * f1;
                uint32_t t = to_ms_since_boot(get_absolute_time()) - t0;
                printf("%ld,%.3f,%ld,%.3f,%lu\n", r0, f0, r1, f1, t);

                if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {
                    printf("STOP\n");
                    break;
                }
            }
            continue;
        }

        // ---- batch mode: capture n samples ----
        if (n > MAX_SAMPLES) n = MAX_SAMPLES;
        float f0 = (float)hx711_read(PIN_SCK0, PIN_DT0);
        float f1 = (float)hx711_read(PIN_SCK1, PIN_DT1);
        uint32_t t_start = to_ms_since_boot(get_absolute_time());
        for (int i = 0; i < n; i++) {
            int32_t r0 = hx711_read(PIN_SCK0, PIN_DT0);
            int32_t r1 = hx711_read(PIN_SCK1, PIN_DT1);
            f0 = IIR_ALPHA * (float)r0 + (1.0f - IIR_ALPHA) * f0;
            f1 = IIR_ALPHA * (float)r1 + (1.0f - IIR_ALPHA) * f1;
            raw0_buf[i]  = r0;
            raw1_buf[i]  = r1;
            filt0_buf[i] = f0;
            filt1_buf[i] = f1;
            time_buf[i]  = to_ms_since_boot(get_absolute_time()) - t_start;
        }
        printf("BEGIN %d\n", n);
        for (int i = 0; i < n; i++) {
            printf("%ld,%.3f,%ld,%.3f,%lu\n",
                   raw0_buf[i], filt0_buf[i],
                   raw1_buf[i], filt1_buf[i], time_buf[i]);
        }
        printf("END\n");
    }
}
