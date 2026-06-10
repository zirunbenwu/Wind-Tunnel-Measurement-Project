# Combined motor tuning client.
#
# PHASE 1 - current loop (text menu + ITEST plot):
#   a  read current (mA)
#   f  set duty (turn motor on)
#   g  set current gains (Kp Ki)
#   h  get current gains
#   k  run ITEST current step + plot
#   p  stop motor
#   l  ---> LOCK current gains and switch to PHASE 2 (live RPM tuning)
#   q  quit
#
# PHASE 2 - velocity/RPM loop (live scrolling plot, type in terminal):
#   <number>      set target RPM        (e.g. 120)
#   b Kp Ki Kd    set velocity gains    (e.g. b 0.05 0.2 0.001)
#   c             print velocity gains
#   p             stop motor
#   q             quit
#
# The current gains set in Phase 1 stay active in Phase 2 because they live
# in the firmware (same serial session, no reboot between phases).
#
# Maps to motor_test.c:
#   a=current  f=duty  g/h=current gains  k=ITEST
#   v=set rpm  b/c=velocity gains  s=read rpm  p=stop
#
# Run:    python motor_tuner.py
# Needs:  pip install pyserial matplotlib

import serial
import time
import threading
import collections
from statistics import mean
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# ---- serial settings (match your firmware) ----
PORT     = 'COM13'
BAUDRATE = 115200

# ---- remembered current-loop gains (set in phase 1, shown in phase 2) ----
current_kp = 0.0
current_ki = 0.0

ser = serial.Serial(PORT, BAUDRATE, timeout=30)
print('Opening port:', ser.name)
time.sleep(2)
ser.reset_input_buffer()


def read_line():
    # Read lines until one that isn't blank or an 'Unknown command' echo.
    while True:
        line = ser.read_until(b'\n')
        if b'Unknown' not in line and line.strip() != b'':
            return line


def read_plot_matrix(title='Test', ylabel='Value'):
    # First line = sample count, then that many "desired actual" rows.
    n_int = int(float(read_line()))
    print('Data length =', n_int)
    ref, data = [], []
    while len(ref) < n_int:
        parts = list(map(float, ser.read_until(b'\n').split()))
        if len(parts) == 2:
            ref.append(parts[0]); data.append(parts[1])
    score = mean(abs(r - d) for r, d in zip(ref, data))
    t = range(len(ref))
    plt.figure()
    plt.plot(t, ref,  'r*-', label='Desired')
    plt.plot(t, data, 'b*-', label='Actual')
    plt.title(title + '  Score=' + str(round(score, 2)))
    plt.ylabel(ylabel); plt.xlabel('Sample index')
    plt.legend(); plt.grid(True, linestyle='--', alpha=0.5)
    plt.tight_layout(); plt.show()


# =====================================================================
#  PHASE 1 - current loop tuning (blocking text menu)
# =====================================================================
def phase1_current():
    global current_kp, current_ki
    while True:
        print('\n=== PHASE 1: CURRENT LOOP ===')
        print('\ta: read current (mA)')
        print('\tf: set duty (turn motor on)')
        print('\tg: set current gains (Kp Ki)')
        print('\th: get current gains')
        print('\tk: ITEST current step + plot')
        print('\tt: read encoder counts')
        print('\ts: read measured RPM')
        print('\ty: read encoder angle (degrees)')
        print('\tz: zero encoder (counts + angle)')
        print('\tp: stop motor')
        print('\tl: LOCK gains -> go to live RPM tuning')
        print('\tq: quit')

        sel = input('\nENTER COMMAND: ').strip()
        if sel == '':
            continue
        ser.reset_input_buffer()   # drop any stale/unread reply before sending
        ser.write((sel + '\n').encode())

        if sel == 'a':
            print('Current =', float(read_line()), 'mA\n')

        elif sel == 'f':
            duty = float(input('Enter duty cycle (-100..100): '))
            ser.write((str(duty) + '\n').encode())
            print('Motor ON - duty', duty, '%\n')

        elif sel == 'g':
            gains = list(map(float, input('Enter Kp and Ki: ').split()))
            ser.write((str(gains[0]) + ' ' + str(gains[1]) + '\n').encode())
            current_kp, current_ki = gains[0], gains[1]
            print('Kp =', current_kp, ' Ki =', current_ki, '\n')

        elif sel == 'h':
            gains = list(map(float, read_line().split()))
            current_kp, current_ki = gains[0], gains[1]
            print('Kp_current =', current_kp)
            print('Ki_current =', current_ki, '\n')

        elif sel == 'k':
            print('Running ITEST...')
            read_plot_matrix('Current Test  kp=' + str(current_kp) +
                             '  ki=' + str(current_ki), 'Current (mA)')

        elif sel == 't':
            print('Encoder counts =', int(float(read_line())), '\n')

        elif sel == 's':
            print('Measured RPM =', float(read_line()), '\n')

        elif sel == 'y':
            print('Angle =', float(read_line()), 'degrees\n')

        elif sel == 'z':
            print('Encoder zeroed (counts + angle = 0)\n')

        elif sel == 'p':
            print('Motor stopped\n')

        elif sel == 'l':
            print('\nCurrent gains locked: Kp =', current_kp,
                  ' Ki =', current_ki)
            print('Switching to live RPM tuning...\n')
            return True       # proceed to phase 2

        elif sel == 'q':
            ser.write(b'p\n')
            return False       # quit entirely

        else:
            print('Invalid selection:', sel)


# =====================================================================
#  PHASE 2 - live RPM plot + tuning
# =====================================================================
WINDOW_S = 20.0
POLL_HZ  = 15
CURRENT_EVERY = 5   # poll current once every N frames (slower than RPM)

ser_lock = threading.Lock()
target_rpm = 0.0
vel_gains = (0.0, 0.0, 0.0)   # Kp Ki Kd, shown on the live plot
running = True


def send(line):
    with ser_lock:
        ser.write((line + '\n').encode())


def read_reply():
    with ser_lock:
        for _ in range(5):
            line = ser.readline()
            if line and b'Unknown' not in line and line.strip() != b'':
                return line.decode(errors='ignore').strip()
    return None


def poll_rpm():
    send('s')
    r = read_reply()
    if r is None:
        return None
    try:
        return float(r)
    except ValueError:
        return None


def poll_current():
    send('a')                  # firmware 'a' returns current in mA
    r = read_reply()
    if r is None:
        return None
    try:
        return float(r)
    except ValueError:
        return None


def rpm_input_thread():
    global target_rpm, running, vel_gains
    print('LIVE RPM TUNER  (type here)')
    print('  current gains in use: Kp =', current_kp, ' Ki =', current_ki)
    print('  <number>     set target RPM')
    print('  b Kp Ki Kd   set velocity gains')
    print('  c            print velocity gains')
    print('  i            read current (mA)')
    print('  p            stop motor')
    print('  q            quit\n')
    while running:
        try:
            cmd = input().strip()
        except (EOFError, KeyboardInterrupt):
            running = False; send('p'); break
        if cmd == '':
            continue
        if cmd == 'q':
            running = False; send('p'); break
        elif cmd == 'p':
            target_rpm = 0.0; send('v'); send('0'); print('STOP')
        elif cmd == 'c':
            send('c'); print('velocity gains (Kp Ki Kd) =', read_reply())
        elif cmd == 'i':
            send('a')                      # firmware 'a' returns current in mA
            print('Current =', read_reply(), 'mA')
        elif cmd.startswith('b'):
            parts = cmd.split()
            if len(parts) == 4:
                send('b'); send(parts[1] + ' ' + parts[2] + ' ' + parts[3])
                vel_gains = (float(parts[1]), float(parts[2]), float(parts[3]))
                print('set velocity gains:', parts[1], parts[2], parts[3])
            else:
                print('usage: b Kp Ki Kd')
        else:
            try:
                val = float(cmd)
                target_rpm = val; send('v'); send(str(val))
                print('target RPM =', val)
            except ValueError:
                print('unknown command:', cmd)


def phase2_rpm():
    global running
    t0 = time.time()
    times  = collections.deque()
    rpms   = collections.deque()

    fig, axr = plt.subplots(figsize=(8, 5))

    line_actual, = axr.plot([], [], 'b-', lw=1.5, label='Actual')
    line_target, = axr.plot([], [], 'r--', lw=1.5, label='Target')
    axr.set_xlabel('Time (s)'); axr.set_ylabel('RPM')
    axr.set_title('Closed-loop RPM')
    axr.grid(True, linestyle='--', alpha=0.5); axr.legend(loc='upper left')
    gain_text = axr.text(0.98, 0.97, '', transform=axr.transAxes,
                        ha='right', va='top', fontsize=10, family='monospace',
                        bbox=dict(boxstyle='round', fc='white', ec='gray', alpha=0.8))

    def update(_):
        if not running:
            plt.close(fig); return line_actual, line_target, gain_text
        now = time.time() - t0
        rpm = poll_rpm()
        if rpm is not None:
            times.append(now); rpms.append(rpm)
        while times and times[0] < now - WINDOW_S:
            times.popleft(); rpms.popleft()

        if times:
            line_actual.set_data(times, rpms)
            line_target.set_data([times[0], times[-1]], [target_rpm, target_rpm])
            axr.set_xlim(max(0, now - WINDOW_S), max(WINDOW_S, now))
            lo = min(min(rpms), target_rpm, 0)
            hi = max(max(rpms), target_rpm, 10)
            m = 0.1 * (hi - lo) + 5
            axr.set_ylim(lo - m, hi + m)
        kp, ki, kd = vel_gains
        rpm_now = rpms[-1] if rpms else 0.0
        gain_text.set_text('target = %.0f RPM\nactual = %.0f RPM\n'
                           'Kp = %g\nKi = %g\nKd = %g'
                           % (target_rpm, rpm_now, kp, ki, kd))
        return line_actual, line_target, gain_text

    threading.Thread(target=rpm_input_thread, daemon=True).start()
    ani = animation.FuncAnimation(fig, update, interval=int(1000 / POLL_HZ),
                                  blit=False, cache_frame_data=False)
    plt.tight_layout()
    plt.show()

    running = False
    time.sleep(0.2)
    with ser_lock:
        ser.write(b'p\n')


# =====================================================================
#  main: phase 1 -> (l) -> phase 2
# =====================================================================
if __name__ == '__main__':
    go_phase2 = phase1_current()
    if go_phase2:
        phase2_rpm()
    try:
        ser.write(b'p\n')      # final safety stop
    except Exception:
        pass
    ser.close()
    print('Closed.')