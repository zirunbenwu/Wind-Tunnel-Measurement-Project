#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

#define NUM_CELLS   3

// SCK, DT for each cell: cell1, cell2, cell3
static const uint PIN_SCK[NUM_CELLS] = {16, 18, 20};
static const uint PIN_DT [NUM_CELLS] = {17, 19, 21};

#define MAX_SAMPLES  4000
#define IIR_ALPHA    0.15f

static int32_t  raw_buf[NUM_CELLS][MAX_SAMPLES];
static float    filt_buf[NUM_CELLS][MAX_SAMPLES];
static uint32_t time_buf[MAX_SAMPLES];

void hx711_init(void) {
    for (int c = 0; c < NUM_CELLS; c++) {
        gpio_init(PIN_SCK[c]);
        gpio_set_dir(PIN_SCK[c], GPIO_OUT);
        gpio_put(PIN_SCK[c], 0);
        gpio_init(PIN_DT[c]);
        gpio_set_dir(PIN_DT[c], GPIO_IN);
    }
}

int32_t hx711_read(int c) {
    while (gpio_get(PIN_DT[c])) {
        tight_loop_contents();
    }

    uint32_t raw = 0;
    // SCK held HIGH > ~60 us powers down the HX711, so block IRQs.
    uint32_t irq = save_and_disable_interrupts();

    for (int i = 0; i < 24; i++) {
        gpio_put(PIN_SCK[c], 1);
        busy_wait_us(1);
        raw = (raw << 1) | gpio_get(PIN_DT[c]);
        gpio_put(PIN_SCK[c], 0);
        busy_wait_us(1);
    }
    // 25th pulse: select channel A, gain 128 for next conversion.
    gpio_put(PIN_SCK[c], 1);
    busy_wait_us(1);
    gpio_put(PIN_SCK[c], 0);
    busy_wait_us(1);

    restore_interrupts(irq);

    // sign-extend 24-bit two's complement to 32-bit signed int
    if (raw & 0x800000) {
        raw |= 0xFF000000;
    }
    return (int32_t)raw;
}

int main(void) {
    stdio_init_all();
    hx711_init();

    // first reading on each cell uses wrong gain/channel, discard
    for (int c = 0; c < NUM_CELLS; c++) (void)hx711_read(c);

    while (true) {
        int n = 0;
        if (scanf("%d", &n) != 1) {
            getchar();          // drain bad char so scanf can recover
            continue;
        }
        if (n < 0) continue;

        // ---- streaming mode: send 0 ----
        if (n == 0) {
            printf("STREAM\n");
            float filt[NUM_CELLS];
            for (int c = 0; c < NUM_CELLS; c++) filt[c] = (float)hx711_read(c);
            absolute_time_t t_start = get_absolute_time();
            while (true) {
                int32_t r[NUM_CELLS];
                for (int c = 0; c < NUM_CELLS; c++) {
                    r[c] = hx711_read(c);
                    filt[c] = IIR_ALPHA * (float)r[c] + (1.0f - IIR_ALPHA) * filt[c];
                }
                uint32_t t = to_ms_since_boot(get_absolute_time())
                           - to_ms_since_boot(t_start);
                printf("%ld,%.3f,%ld,%.3f,%ld,%.3f,%lu\n",
                       (long)r[0], filt[0], (long)r[1], filt[1],
                       (long)r[2], filt[2], (unsigned long)t);

                if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {
                    printf("STOP\n");
                    break;
                }
            }
            continue;
        }

        // ---- batch mode ----
        if (n > MAX_SAMPLES) n = MAX_SAMPLES;

        float filt[NUM_CELLS];
        for (int c = 0; c < NUM_CELLS; c++) {
            int32_t first = hx711_read(c);
            filt[c] = (float)first;
            raw_buf[c][0]  = first;
            filt_buf[c][0] = filt[c];
        }
        absolute_time_t t_start = get_absolute_time();
        time_buf[0] = 0;

        for (int i = 1; i < n; i++) {
            for (int c = 0; c < NUM_CELLS; c++) {
                int32_t r = hx711_read(c);
                filt[c] = IIR_ALPHA * (float)r + (1.0f - IIR_ALPHA) * filt[c];
                raw_buf[c][i]  = r;
                filt_buf[c][i] = filt[c];
            }
            time_buf[i] = to_ms_since_boot(get_absolute_time())
                        - to_ms_since_boot(t_start);
        }

        printf("BEGIN %d\n", n);
        for (int i = 0; i < n; i++) {
            printf("%ld,%.3f,%ld,%.3f,%ld,%.3f,%lu\n",
                   (long)raw_buf[0][i], filt_buf[0][i],
                   (long)raw_buf[1][i], filt_buf[1][i],
                   (long)raw_buf[2][i], filt_buf[2][i],
                   (unsigned long)time_buf[i]);
        }
        printf("END\n");
    }
}