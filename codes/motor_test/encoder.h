#ifndef ENCODER_H
#define ENCODER_H
 
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "quadrature_encoder.pio.h"
 
void initEncoder();
int getEncoder();
void setEncoderToZero();
// Velocity in RPM, computed from the count delta since the previous call.
// Pass the loop period in seconds (e.g. 0.005f for a 200 Hz loop).
float getEncoderRPM(float dt_seconds);
 
#endif