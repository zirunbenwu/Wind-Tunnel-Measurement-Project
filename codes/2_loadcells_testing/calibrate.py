#!/usr/bin/env python3
"""
calibrate.py  -  interactive tare + known-weight calibration for N HX711 load cells.

Firmware: BATCH mode. PC sends "<n>\\n"; Pico replies:
    BEGIN n
    <fields...>            # one CSV line per sample
    END
Line format is  raw0,filt0,raw1,filt1,[raw2,filt2,]...,t_ms
i.e. two columns (raw,filt) per cell, then a trailing t_ms.

Set NUM_CELLS below: 2 today, 3 later (no other change needed).

Each run:
  1. TARE every cell automatically (average a no-load batch -> offset).
  2. For each cell: place a known weight, type its grams, capture -> scale (counts/gram).
     Scales are saved to calibration.json and reused; tare is always fresh.
  3. Live readout of force per cell (press Ctrl+C to stop).

Usage:
    python calibrate.py COM6
    python calibrate.py COM6 --units N
    python calibrate.py COM6 --recalibrate         # redo known-weight step
"""

import sys, time, json, os, argparse
import numpy as np
import serial

# ----------------------------------------------------------------------
NUM_CELLS    = 2            # <-- set to 3 when you add the third cell
BAUD         = 115200
TARE_SAMPLES = 160          # ~2 s at 80 Hz
CAL_SAMPLES  = 160          # samples averaged while a known weight sits on the cell
SETTLE_S     = 2.0          # USB CDC settle after opening the port (Windows)
FS_NOMINAL   = 80.0
CAL_FILE     = "calibration.json"
G            = 9.81
# ----------------------------------------------------------------------


def collect(ser, n):
    """Send n, read back n lines, return array shape (n, NUM_CELLS) of RAW counts."""
    ser.reset_input_buffer()
    ser.write(f"{n}\n".encode())

    t0 = time.time()
    while True:
        line = ser.readline().decode(errors="ignore").strip()
        if line.startswith("BEGIN"):
            break
        if time.time() - t0 > 5:
            raise RuntimeError("Timed out waiting for BEGIN from Pico")

    raw = np.full((n, NUM_CELLS), np.nan)
    deadline = time.time() + n / FS_NOMINAL + 10
    i = 0
    while i < n:
        line = ser.readline().decode(errors="ignore").strip()
        if not line:
            if time.time() > deadline:
                raise RuntimeError(f"Timed out after {i}/{n} samples")
            continue
        if line.startswith("END"):
            break
        parts = line.split(",")
        # expect 2*NUM_CELLS + 1 fields; raw of cell k is index 2*k
        if len(parts) < 2 * NUM_CELLS + 1:
            continue
        try:
            for k in range(NUM_CELLS):
                raw[i, k] = float(parts[2 * k])
            i += 1
        except ValueError:
            continue
    return raw[:i]


def average_capture(ser, n, what):
    print(f"  capturing {n} samples ...")
    raw = collect(ser, n)
    mean = np.nanmean(raw, axis=0)
    std  = np.nanstd(raw, axis=0)
    for k in range(NUM_CELLS):
        print(f"  cell {k}: {what} = {mean[k]:10.1f} counts  (noise sigma {std[k]:.1f})")
    return mean


def tare(ser):
    print("\n=== TARE ===")
    input("Make sure NOTHING is on any load cell (motor idle), then press Enter...")
    return average_capture(ser, TARE_SAMPLES, "offset")


def calibrate(ser, offset):
    print("\n=== CALIBRATE (known weight per cell) ===")
    scale = np.zeros(NUM_CELLS)
    for k in range(NUM_CELLS):
        print(f"\n-- Cell {k} --")
        mass = float(input(f"  Enter the known weight you'll place on cell {k}, in GRAMS: ").strip())
        input(f"  Place the {mass:g} g weight on cell {k}, then press Enter...")
        loaded = average_capture(ser, CAL_SAMPLES, "loaded")[k]
        scale[k] = (loaded - offset[k]) / mass
        print(f"  -> cell {k} scale = {scale[k]:.4f} counts/gram")
        input(f"  Remove the weight from cell {k}, then press Enter...")
    with open(CAL_FILE, "w") as fh:
        json.dump({"num_cells": NUM_CELLS, "scale": scale.tolist()}, fh, indent=2)
    print(f"\nSaved scales to {CAL_FILE}")
    return scale


def load_scale():
    if not os.path.exists(CAL_FILE):
        return None
    with open(CAL_FILE) as fh:
        d = json.load(fh)
    if d.get("num_cells") != NUM_CELLS:
        print(f"calibration.json has {d.get('num_cells')} cells but NUM_CELLS={NUM_CELLS}; recalibrating.")
        return None
    return np.array(d["scale"], dtype=float)


def live_readout(ser, offset, scale, units):
    print("\n=== LIVE FORCE (Ctrl+C to stop) ===")
    k_to_unit = (G / 1000.0) if units == "N" else 1.0
    label = "N" if units == "N" else "g"
    try:
        while True:
            raw = collect(ser, 8)             # short batch, ~0.1 s
            mean = np.nanmean(raw, axis=0)
            force = (mean - offset) / scale * k_to_unit
            print("  " + "   ".join(
                f"cell{k}: {force[k]:8.2f} {label}" for k in range(NUM_CELLS)),
                end="\r", flush=True)
    except KeyboardInterrupt:
        print("\nstopped.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--units", choices=["g", "N"], default="g")
    ap.add_argument("--recalibrate", action="store_true")
    args = ap.parse_args()

    print(f"Opening {args.port} ...")
    with serial.Serial(args.port, BAUD, timeout=1) as ser:
        time.sleep(SETTLE_S)

        offset = tare(ser)

        scale = load_scale()
        if scale is None or args.recalibrate:
            scale = calibrate(ser, offset)
        else:
            print("\nUsing saved scales (run with --recalibrate to redo):")
            for k in range(NUM_CELLS):
                print(f"  cell {k}: {scale[k]:.4f} counts/gram")

        # guard against a zero/garbage scale
        if np.any(scale == 0):
            print("Warning: a scale is zero; that cell will read inf. Recalibrate it.")

        live_readout(ser, offset, scale, args.units)


if __name__ == "__main__":
    main()
