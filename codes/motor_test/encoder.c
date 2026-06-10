#include "encoder.h"
 
PIO pio = pio0;
const uint sm = 0;
 
// encoder pins must be next to each other
#define ENCODER_FIRST_PIN 2
volatile int encoderOffset = 0;
 
void initEncoder(){
    // init the PIO for the encoder
 
    pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm, ENCODER_FIRST_PIN, 0);
}
 
int getEncoder(){
    // return the encoder counts
    return quadrature_encoder_get_count(pio, sm) - encoderOffset;
}
 
// set the offset to zero the encoder
void setEncoderToZero(){
    encoderOffset = quadrature_encoder_get_count(pio, sm);
}

// 334 PPR * 4 (quadrature) = 1336 counts per revolution
#define ENC_COUNTS_PER_REV (334 * 4)

// Velocity is averaged over a window of recent samples to cut quantization
// noise. At a 200 Hz call rate, 10 samples = ~50 ms window.
// RPM is computed from the count change across the whole window:
//   rpm = (count_now - count_window_ago) / counts_per_rev / (N*dt) * 60
#define ENC_VEL_WINDOW 10

float getEncoderRPM(float dt_seconds){
    static int   buf[ENC_VEL_WINDOW] = {0};  // ring buffer of recent counts
    static int   idx = 0;                    // next write position
    static int   filled = 0;                 // how many slots are valid
    static bool  primed = false;

    int now = quadrature_encoder_get_count(pio, sm);

    if (!primed) {
        // seed the whole buffer with the starting count
        for (int i = 0; i < ENC_VEL_WINDOW; i++) buf[i] = now;
        primed = true;
        filled = ENC_VEL_WINDOW;
        idx = 0;
        return 0.0f;
    }

    // oldest sample is the one we're about to overwrite
    int oldest = buf[idx];
    buf[idx] = now;
    idx = (idx + 1) % ENC_VEL_WINDOW;

    if (dt_seconds <= 0.0f) return 0.0f;

    // count change over the full window divided by the window's time span
    int   delta   = now - oldest;
    float span_s  = (float)ENC_VEL_WINDOW * dt_seconds;
    return ((float)delta / (float)ENC_COUNTS_PER_REV) / span_s * 60.0f;
}