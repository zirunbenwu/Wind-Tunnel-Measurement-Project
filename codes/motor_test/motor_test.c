#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "ina219.h"
#include "encoder.h"

//H bridge input pins 
#define IN1_PIN           14
#define IN2_PIN           15

//PWM frequency in Hz
#define PWM_FREQ_HZ       20000

//Encoder
#define COUNTS_PER_REV    334
#define QUADRATURE_FACTOR 4
#define COUNTS_PER_DEG    ((COUNTS_PER_REV * QUADRATURE_FACTOR) / 360.0f)

//Current control
#define ITEST_SAMPLES     400
#define ITEST_SEGMENT     100
#define ITEST_CURRENT     100.0f
#define EINT_MAX          1000.0f

//Position integrator clamp
#define EINT_POS_MAX      10000.0f

//Trajectory
#define TRAJ_MAX_LEN      4000

//State machine
enum mode_t {IDLE, PWM, ITEST, HOLD, TRACK, VELOCITY};
volatile enum mode_t state = IDLE;

//H-bridge PWM handles
static uint in1_slice, in1_chan;
static uint in2_slice, in2_chan;
static uint32_t pwm_wrap;

//Global duty cycle
volatile float duty_cycle = 0.0f;

//Current PI gains
volatile float Kp_current = 0.0f;
volatile float Ki_current = 0.0f;

// Position PID gains
volatile float Kp_position = 0.0f;
volatile float Kd_position = 0.0f;
volatile float Ki_position = 0.0f;

// Velocity (RPM) PID gains — outer loop for closed-loop speed control
volatile float Kp_velocity = 0.0f;
volatile float Ki_velocity = 0.0f;
volatile float Kd_velocity = 0.0f;

// Velocity loop setpoint and feedback
volatile float target_rpm   = 0.0f;   // commanded RPM (set by 'v')
volatile float measured_rpm = 0.0f;   // latest measured RPM (reported by 't')

// Velocity loop integrator / derivative memory
static volatile float eint_velocity  = 0.0f;
static volatile float eprev_velocity = 0.0f;

// Velocity loop period: position_control runs at 200 Hz -> 5 ms
#define VEL_DT          0.005f
// Clamp the velocity integrator (anti-windup), in current-output units
// Integrator clamp: must be high enough that Ki*eint can reach VEL_CURRENT_MAX.
// At Ki~0.1 and a 500 mA current cap, eint needs to reach ~5000. The real
// current limit is VEL_CURRENT_MAX below, so this clamp only prevents runaway.
#define EINT_VEL_MAX    5000.0f
// Max current the velocity loop may request from the inner loop (mA)
#define VEL_CURRENT_MAX 500.0f

// ITEST 
volatile float itest_desired[ITEST_SAMPLES];
volatile float itest_actual[ITEST_SAMPLES];
volatile int   itest_index = 0;

//Current PI integrator
static volatile float eint_current = 0.0f;

//Position PID state 
static volatile float eint_position  = 0.0f;
static volatile float eprev_position = 0.0f;

//Trajectory variables
volatile float desired_angle[TRAJ_MAX_LEN];
volatile float actual_angle[TRAJ_MAX_LEN];
volatile int   traj_len       = 0;
volatile int   traj_index     = 0;
volatile float desired_current = 0.0f;  //set by position, used by current

// setDuty() — full half duty cycle with slow decay

void setDuty(float duty) {
    if (duty >  100.0f) duty =  100.0f;
    if (duty < -100.0f) duty = -100.0f;

    uint32_t full = pwm_wrap + 1;
    uint32_t in1_level, in2_level;

    if (duty > 0.0f) {
        //Forward slow decay: IN1=100%, IN2=(100%-duty) 
        in1_level = full;
        in2_level = (uint32_t)((100.0f - duty) / 100.0f * (float)full);
    } else if (duty < 0.0f) {
        // Reverse slow decay: IN1=(100%+duty), IN2=100% 
        in1_level = (uint32_t)((100.0f + duty) / 100.0f * (float)full);
        in2_level = full;
    } else {
        // Brake: IN1=100%, IN2=100% 
        in1_level = full;
        in2_level = full;
    }

    pwm_set_chan_level(in1_slice, in1_chan, in1_level);
    pwm_set_chan_level(in2_slice, in2_chan, in2_level);
}
// hbridge initialization
void init_hbridge(void) {
    pwm_wrap = (clock_get_hz(clk_sys) / PWM_FREQ_HZ) - 1;

    gpio_set_function(IN1_PIN, GPIO_FUNC_PWM);
    in1_slice = pwm_gpio_to_slice_num(IN1_PIN);
    in1_chan  = pwm_gpio_to_channel(IN1_PIN);
    pwm_set_wrap(in1_slice, pwm_wrap);
    pwm_set_clkdiv(in1_slice, 1.0f);
    pwm_set_chan_level(in1_slice, in1_chan, pwm_wrap + 1);

    gpio_set_function(IN2_PIN, GPIO_FUNC_PWM);
    in2_slice = pwm_gpio_to_slice_num(IN2_PIN);
    in2_chan  = pwm_gpio_to_channel(IN2_PIN);
    pwm_set_wrap(in2_slice, pwm_wrap);
    pwm_set_clkdiv(in2_slice, 1.0f);
    pwm_set_chan_level(in2_slice, in2_chan, pwm_wrap + 1);

    pwm_set_enabled(in1_slice, true);
    pwm_set_enabled(in2_slice, true);   // enable both (no-op if same slice)
}


// Current PI controller, called by current_control interrupt
float current_PI(float des_mA, float act_mA) {
    float error = des_mA - act_mA;
    eint_current += error;
    if (eint_current >  EINT_MAX) eint_current =  EINT_MAX;
    if (eint_current < -EINT_MAX) eint_current = -EINT_MAX;
    float u = Kp_current * error + Ki_current * eint_current;
    if (u >  100.0f) u =  100.0f;
    if (u < -100.0f) u = -100.0f;
    return u;
}
 
// position_control — 200Hz interrupt
bool position_control(struct repeating_timer *t) {
    // Update measured RPM every tick, in every state, so 's' and the plot
    // always reflect actual speed (open-loop PWM, closed-loop, or idle).
    measured_rpm = getEncoderRPM(VEL_DT);

    switch (state) {
        case TRACK:
        {
            if (state != TRACK) break;
            // Read actual angle
            float act_deg = (float)getEncoder() / COUNTS_PER_DEG;

            // Get desired angle 
            float des_deg = desired_angle[traj_index];

            // Save actual 
            actual_angle[traj_index] = act_deg;

            // PID Calculations 
            float error = des_deg - act_deg;
            float deriv = error - eprev_position;

            float p_term = Kp_position * error;
            float d_term = Kd_position * deriv;

            // ANTI-WINDUP: Integration Zone
            if (error > -10.0f && error < 10.0f) {
                eint_position += error;
            } else {
                eint_position = 0.0f; 
            }

            // Absolute limits on the integral term to prevent extreme windup inside the zone
            if (eint_position >  EINT_POS_MAX) eint_position =  EINT_POS_MAX;
            if (eint_position < -EINT_POS_MAX) eint_position = -EINT_POS_MAX;

            // Final current calculation
            desired_current = p_term + d_term + (Ki_position * eint_position);

            // Clamp desired current to hardware limits 
            if (desired_current >  500.0f) desired_current =  500.0f;
            if (desired_current < -500.0f) desired_current = -500.0f;

            // Save current error for the next derivative calculation
            eprev_position = error;

            //Advance index 
            traj_index++;
            if (traj_index >= traj_len) {
                traj_index      = 0;
                eint_position   = 0.0f;
                eprev_position  = 0.0f;
                desired_current = 0.0f;
                state = IDLE;
            }
            break;
        }
        case VELOCITY:
        {
            // measured_rpm is already updated at the top of this function
            float error = target_rpm - measured_rpm;
            float deriv = (error - eprev_velocity);   // per-tick derivative

            float p_term = Kp_velocity * error;
            float d_term = Kd_velocity * deriv;

            // Integrate, then clamp (anti-windup)
            eint_velocity += error;
            if (eint_velocity >  EINT_VEL_MAX) eint_velocity =  EINT_VEL_MAX;
            if (eint_velocity < -EINT_VEL_MAX) eint_velocity = -EINT_VEL_MAX;

            desired_current = p_term + d_term + (Ki_velocity * eint_velocity);

            // One-direction drive: never request negative (reverse) current
            if (desired_current < 0.0f)            desired_current = 0.0f;
            if (desired_current > VEL_CURRENT_MAX) desired_current = VEL_CURRENT_MAX;

            eprev_velocity = error;

            // Commanded stop -> fully reset the loop
            if (target_rpm <= 0.0f) {
                eint_velocity   = 0.0f;
                eprev_velocity  = 0.0f;
                desired_current = 0.0f;
            }
            break;
        }
        default: { break; }
    }
    return true;
}
 
// current_control — 1kHz interrupt

bool current_control(struct repeating_timer *t) {
    switch (state) {
        case IDLE:
        {
            setDuty(0.0f);
            break;
        }
        case PWM:
        {
            setDuty(duty_cycle);
            break;
        }
        case ITEST:
        {
            int segment = itest_index / ITEST_SEGMENT;
            if (itest_index % ITEST_SEGMENT == 0) {
                eint_current = 0.0f;
            }
            float des_mA = (segment % 2 == 0) ? -ITEST_CURRENT
                                               : +ITEST_CURRENT;
            float act_mA = read_ina219();
            float u = current_PI(des_mA, act_mA);
            setDuty(u);
            itest_desired[itest_index] = des_mA;
            itest_actual[itest_index]  = act_mA;
            itest_index++;
            if (itest_index >= ITEST_SAMPLES) {
                itest_index  = 0;
                eint_current = 0.0f;
                state = IDLE;
            }
            break;
        }
        case TRACK:
        case VELOCITY:
        {
            float act_mA = read_ina219();
            float u = current_PI(desired_current, act_mA);
            setDuty(u);
            break;
        }
        case HOLD: { break; }
    }
    return true;
}


// Read one line of input from USB into buf using getchar_timeout_us.
// This is the reliable input path on RP2040 USB CDC (scanf is not).
// Blocks until a newline (\n or \r) is received; strips the terminator.
static void read_line_blocking(char *buf, int maxlen) {
    int i = 0;
    while (i < maxlen - 1) {
        int ch = getchar_timeout_us(1000000);   // 1 s slices, retry forever
        if (ch == PICO_ERROR_TIMEOUT) continue;
        if (ch == '\n' || ch == '\r') {
            if (i == 0) continue;                // skip leading blank lines
            break;
        }
        buf[i++] = (char)ch;
    }
    buf[i] = '\0';
}

int main(void) {
    stdio_init_all();

    // Wait until the USB serial (CDC) connection is actually open.
    // Without this, the first bytes after boot can be lost and scanf/printf
    // may never sync with the host. Requires nothing extra in CMake.
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    sleep_ms(500);

    // Boot banner: if you see this in the monitor, the firmware IS running.
    printf("BOOT: motor_test ready\r\n");

    printf("init: ina219...\r\n");
    init_ina219();
    printf("init: ina219 OK\r\n");

    printf("init: hbridge...\r\n");
    init_hbridge();
    printf("init: hbridge OK\r\n");

    printf("init: encoder...\r\n");
    initEncoder();
    printf("init: encoder OK\r\n");

    printf("READY: enter a command\r\n");

    struct repeating_timer current_timer;
    add_repeating_timer_us(-1000, current_control, NULL, &current_timer);

    struct repeating_timer position_timer;
    add_repeating_timer_us(-5000, position_control, NULL, &position_timer);

    char cmd;
    char linebuf[128];

    while (true) {
        // Read the command letter via getchar (reliable over USB CDC)
        int c = getchar_timeout_us(1000000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == '\n' || c == '\r') continue;     // ignore stray line endings
        cmd = (char)c;

        switch (cmd) {

            case 'a':
            {
                float ma = read_ina219();
                printf("%.2f\r\n", ma);
                break;
            }

            case 'd':
            {
                int n = 0;
                read_line_blocking(linebuf, sizeof(linebuf));
                sscanf(linebuf, "%d", &n);
                printf("%d\r\n", n + 1);
                break;
            }

            case 'e':
            {
                int a = 0, b = 0;
                read_line_blocking(linebuf, sizeof(linebuf));
                sscanf(linebuf, "%d %d", &a, &b);
                printf("%d %d\r\n", a + b, a - b);
                break;
            }

            case 'f':
            {
                float duty = 0.0f;
                read_line_blocking(linebuf, sizeof(linebuf));
                sscanf(linebuf, "%f", &duty);
                duty_cycle = duty;
                state = PWM;
                break;
            }

            case 'v':
            {
                // Set target RPM and engage the closed-loop velocity controller
                float rpm = 0.0f;
                read_line_blocking(linebuf, sizeof(linebuf));
                sscanf(linebuf, "%f", &rpm);
                if (rpm < 0.0f) rpm = 0.0f;   // one-direction drive
                target_rpm = rpm;
                // Reset loop memory on (re)entry so there's no stale windup
                eint_velocity   = 0.0f;
                eprev_velocity  = 0.0f;
                eint_current    = 0.0f;
                state = VELOCITY;
                break;
            }

            case 'b':
            {
                // Set velocity (RPM) loop gains: Kp Ki Kd
                float kp = 0.0f, ki = 0.0f, kd = 0.0f;
                read_line_blocking(linebuf, sizeof(linebuf));
                sscanf(linebuf, "%f %f %f", &kp, &ki, &kd);
                Kp_velocity = kp;
                Ki_velocity = ki;
                Kd_velocity = kd;
                break;
            }

            case 'c':
            {
                // Get velocity (RPM) loop gains
                printf("%.4f %.4f %.4f\r\n",
                       Kp_velocity, Ki_velocity, Kd_velocity);
                break;
            }

            case 'g':
            {
                float kp = 0.0f, ki = 0.0f;
                read_line_blocking(linebuf, sizeof(linebuf));
                sscanf(linebuf, "%f %f", &kp, &ki);
                Kp_current = kp;
                Ki_current = ki;
                break;
            }

            case 'h':
            {
                printf("%.4f %.4f\r\n", Kp_current, Ki_current);
                break;
            }

            case 'i':
            {
                float kp = 0.0f, kd = 0.0f, ki = 0.0f;
                read_line_blocking(linebuf, sizeof(linebuf));
                sscanf(linebuf, "%f %f %f", &kp, &kd, &ki);
                Kp_position = kp;
                Kd_position = kd;
                Ki_position = ki;
                break;
            }

            case 'j':
            {
                printf("%.4f %.4f %.4f\r\n",
                       Kp_position, Kd_position, Ki_position);
                break;
            }

            case 'k':
            {
                eint_current = 0.0f;
                itest_index  = 0;
                state = ITEST;
                while (state == ITEST) {
                    tight_loop_contents();
                }
                printf("%d\r\n", ITEST_SAMPLES);
                for (int i = 0; i < ITEST_SAMPLES; i++) {
                    printf("%.2f %.2f\r\n", itest_desired[i], itest_actual[i]);
                }
                break;
            }

            case 'm':
            case 'n':
            {
                int count = 0;
                read_line_blocking(linebuf, sizeof(linebuf));
                sscanf(linebuf, "%d", &count);
                if (count > TRAJ_MAX_LEN) count = TRAJ_MAX_LEN;
                traj_len = count;
                for (int i = 0; i < traj_len; i++) {
                    float angle = 0.0f;
                    read_line_blocking(linebuf, sizeof(linebuf));
                    sscanf(linebuf, "%f", &angle);
                    desired_angle[i] = angle;
                }
                printf("Trajectory loaded: %d points\r\n", traj_len);
                break;
            }

            case 'o':
            {
                // Reset state, zero encoder, start tracking
                setEncoderToZero();
                traj_index    = 0;
                eint_position = 0.0f;
                eprev_position = 0.0f;
                eint_current  = 0.0f;
                desired_current = 0.0f;
                state = TRACK;

                // Wait until position_control finishes trajectory 
                while (state == TRACK) {
                    tight_loop_contents();
                }

                // Print results: n_points, then desired actual pairs 
                printf("%d\r\n", traj_len);
                for (int i = 0; i < traj_len; i++) {
                    printf("%.2f %.2f\r\n", desired_angle[i], actual_angle[i]);
                }
                break;
            }

            case 'p':
            {
                    target_rpm      = 0.0f;
                    desired_current = 0.0f;
                    eint_current    = 0.0f;
                    eint_position   = 0.0f;
                    eint_velocity   = 0.0f;
                    eprev_velocity  = 0.0f;
                    setDuty(0.0f);
                    state = IDLE;
                    break;
            }

            case 'r':
            {
                printf("%d\r\n", (int)state);
                break;
            }

            case 't':
            {
                int counts = getEncoder();
                printf("%d\r\n", counts);
                break;
            }

            case 's':
            {
                // Report the latest measured speed in RPM (for the live plot)
                printf("%.2f\r\n", measured_rpm);
                break;
            }

            case 'y':
            {
                int counts = getEncoder();
                float angle = (float)counts / COUNTS_PER_DEG;
                printf("%.2f\r\n", angle);
                break;
            }

            case 'z':
            {
                setEncoderToZero();
                break;
            }

            case 'q':
            {
                state = IDLE;
                break;
            }

            default:
            {
                printf("Unknown command: %c\r\n", cmd);
                break;
            }
        }
    }
}