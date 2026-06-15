# Report Structure & Offline Work Plan

## The framing shift (read first)
You do **not** have wind tunnel data, only static (bench) testing. So the report's
claim is NOT "we validated CFD." The honest, complete claim is:

> "We designed, built, and **bench-validated** a modular instrumentation platform and
> scale model for future wind-tunnel CFD verification. Static calibration confirms the
> load-cell and servo subsystems perform to spec; wind-tunnel testing is the next phase."

This reframes wind-tunnel results as **Future Work**, which is normal and expected for a
build/instrumentation project. Everything below follows from that.

---

## What you currently have (your evidence)
- Load cell code + **static test** (calibration: known mass → measured force)
- Servo code + **static test** (commanded speed/position → measured response)
- Full **CAD** of the scale car + platform
- Designed & (partially) built hardware
- NO wind tunnel runs, NO PIV, NO CFD comparison yet

---

## Recommended report structure

### Title
Reframe to what was actually done. Suggested:
"Design and Bench Validation of a Modular Instrumentation Platform for Wind-Tunnel
CFD Verification of a Formula-Style Vehicle"
(Drop the "XX / X" placeholders. If you keep the variables framing, your real study
variables are ground simulation and blockage ratio — but those are Future Work, so the
"design & validation" title is more honest for what you have.)

### Abstract
Edit the last sentence so it doesn't over-claim. End with bench-validation result +
future work, e.g.: "...Static calibration confirms accurate force and speed measurement;
wind-tunnel testing and CFD comparison are identified as the next phase."

### I. Introduction  (mostly done — keep)
- Add one sentence at the end stating the report's actual scope: design + bench validation.

### II. Methods
1. **Test Facility & Constraints**  ← put the wind tunnel constraints here, up front
   - 0.6 m × 0.6 m (0.36 m²) section; 40 m/s max
   - Explain these drive: (a) blockage ratio → platform frontal area, (b) max velocity →
     achievable Reynolds → model scale (1/5)
2. **Testing Platform**
   - Design (load-cell layout, roller bearings, frontal-area minimization → cite 0.36 m²)
   - Manufacturing (80/20, acrylic, polycarbonate, 3D-printed curves, load-cell housing)
3. **Scale FSAE Car**
   - Design (Reynolds matching → 1/5 scale → cite 40 m/s; interchangeable aero)
   - Manufacturing (PLA, pressfit pegs, glued wings)
4. **Electronics & Data Acquisition**
   - Load-cell circuit + DAQ logic (describe code: sampling, conversion, live view)
   - Servo control (describe code: command → wheel speed, user control)
5. **Static Test Procedures**  ← this is where your real testing lives
   - Load-cell calibration procedure (apply known masses, record output)
   - Servo characterization procedure (command vs. measured response)

### III. Results  (only report what you have)
- **Load-cell static calibration** — calibration curve, linearity/R², resolution, noise
- **Servo static test** — commanded vs. actual, response/settling, repeatability
- **Model & platform build** — completed CAD + as-built photos (a "result" of the build)
- REMOVE from Results: rolling-wheel validation, CFD comparison (move to Future Work)
- (Keep "instrument box CFD" only if you actually ran that thermal/flow sim; else cut)

### IV. Discussion
- What the static results show: subsystems meet spec, sources of noise/error
- Limitations: no aerodynamic loading yet, calibration done off-tunnel, blockage estimate
  is predicted not measured
- What this means for readiness: platform is ready for wind-tunnel commissioning

### V. Future Work  ← NEW section; this carries the wind-tunnel story honestly
- Wind-tunnel commissioning & in-flow calibration
- Rolling-wheel ground-simulation validation
- Drag/downforce measurement across aero configs
- PIV flow-field characterization
- CFD comparison & methodology document for Northwestern FSAE

### VI. References

### Appendices
- A: CAD drawings + part references
- B: Additional drawings
- C: Procedures/checklists (fill the template stubs OR delete if unused)
- D (optional): Code listings — load-cell + servo (or link a repo)

---

---

## Reynolds & Blockage — drop this into "Methods → Test Facility & Constraints"
**Where it goes in the report:** the top of Methods, as the first subsection
(Test Facility & Constraints), BEFORE the platform and car subsections — because both
the platform size and the car scale derive from it. Then cite the blockage number again
in Testing Platform → Design, and the Reynolds number again in Scale Car → Design.

### The constraints (REAL numbers from Research Notes)
- Test section: 0.6 m × 0.6 m  →  A_tunnel = 0.36 m²;  test-section length 1.5 m
- Max freestream velocity: 40 m/s
- Full car: length 2.8 m, width 1.4 m, max velocity 13.4 m/s (= 30 mph)
- CFD verification target: ANSYS Fluent at 30 m/s constant inlet velocity

### Reynolds number (Re = ρ·V·L / μ = u·L / ν)
- Dynamic similarity → matching Re makes the model's C_d, C_l equal the full car's.
- Same fluid (air), so matching Re needs V_model · L_model = V_full · L_full.
- At 5:1 scale, exact match would need V_model = 5 × 13.4 ≈ 67 m/s > 40 m/s limit → cannot match exactly.
- Ratio achieved (length-independent):
  Re_model / Re_full = (40 / 13.4) × (1/5) ≈ 0.60  → model reaches ~60% of full-scale Re.
- Absolute values (from Research Notes):
  - Full car @ 13.4 m/s (L = 2.8 m):  **Re ≈ 2.54×10⁶**
  - Scaled car @ 40 m/s (L = 0.56 m): **Re ≈ 1.51×10⁶**
- Justification = **Reynolds independence**: above a critical Re both flows are fully
  turbulent and the coefficients plateau, so ~60% of full-scale Re is still representative.

### Blockage ratio (BR = A_frontal / A_tunnel)
- A_frontal = model **+ rig/strut** projected into the flow.
- Guideline / cited target: keep ≤ ~10% of total area [ref 1]; above ~10% apply a
  correction (e.g. Maskell).
- **Achieved: 5:1 scale → BR = 11.1%** (right at the limit → state whether you corrected;
  rig frontal area adds to this).

### Why 5:1 scale (the tradeoff sentence for the report)
Bigger model → higher Re (good) but higher blockage (bad). 5:1 is the largest model that
keeps blockage near the acceptable limit (11.1%) while maximizing achievable Reynolds number.

### Predicted forces (these size your sensors — use in Methods → Electronics + Results)
| Quantity            | Full car  | Scaled car (5:1) |
|---------------------|-----------|------------------|
| Total downforce (N) | 262.6     | **10.505**       |
| Total drag (N)      | 106.7     | **4.268**        |
- Sensor sizing check: 3 kg load cells ≈ 29.4 N each — comfortably above the ~10.5 N
  downforce and ~4.3 N drag predicted, so the choice is justified. Say this explicitly.
- Force coefficients used: C_d = 2F_d / (ρu²A),  C_L = 2L / (ρV²S).
- Reference frontal area (drag): 1.067 m² (full).  Surface area (downforce, block model): 14.26 m².
- CFD baseline (full car, from notes): Overall C_D ≈ 0.789, Overall C_L ≈ -1.48,
  frontal area 1.23 m², car mass 227 kg.

### Ready-to-paste paragraph
> "Matching full-scale Reynolds number (≈2.54×10⁶ at 13.4 m/s) would require ~67 m/s at
> 5:1 scale, exceeding the tunnel's 40 m/s limit. The model therefore operates at
> Re ≈ 1.51×10⁶, ~60% of full scale. As both flows are fully turbulent and above the
> Reynolds-independence threshold for a bluff body, the measured force coefficients
> remain representative. The 5:1 scale was selected as the largest model keeping blockage
> within the ~10% acceptable limit (achieved 11.1%) while maximizing achievable Reynolds
> number."

**TODO offline:** confirm whether you apply a blockage correction at 11.1%, and supply
the full citation for reference [1] (the ~10% blockage guideline).

---

## Background Research — where each piece goes in the report
(from Research Notes.pdf)

- **Blockage guideline "ideally 10% of total area" [1]** → Methods → Test Facility &
  Constraints (cite [1]). Add the source to References.
- **Full-scale vs. scaled parameter table (length, velocity, Re, forces)** → Methods →
  Test Facility & Constraints; reproduce as a table. Also seeds Results (predicted forces).
- **Sensor / gauge sizing (predicted forces → load-cell capacity)** → Methods →
  Electronics & DAQ: justify 3 kg load cells from the ~10.5 N / ~4.3 N predictions.
- **Force-coefficient equations (Re, C_d, C_L)** → Methods (Test Facility for Re;
  Data-reduction subsection for C_d, C_L) — show how raw load-cell force → coefficient.
- **Moving wheels / moving ground justification (X% error)** → Introduction (motivation)
  AND Methods → Scale Car Design (why rolling wheels). NOTE: the % error figure is still
  a "[fact check]" placeholder — needs a source or your own CFD comparison.
- **Pressure theory (q = ½ρV², p = ρgh, p = p₀+ρgh) + pressure-vs-PIV decision** →
  Methods → Data Acquisition (scope justification) and/or Discussion. Key argument to
  include: pressure taps were deprioritized because PIV + smoke already visualize flow
  separation; pressure would only add center-of-pressure data, which is secondary to the
  goal of verifying C_d and C_l.
- **Project goals (verify C_d, C_l; verify flow separation)** → Introduction (objectives).
  "Force plate → C_d, C_l; PIV + smoke machine → flow separation."
- **Budget (~$500–$1000; $1–5 per pressure tap)** → Methods (design constraints) or an
  appendix; supports the decision to limit pressure instrumentation.
- **CFD baseline table (per-component downforce, CoP, overall C_D/C_L)** → Background or
  Results-comparison (this is the CFD prediction your experiment will eventually verify).

---

## OFFLINE TO-DO (priority order — do these now)
1. **Fix the title** and the abstract's last sentence (5 min, high impact).
2. **Write the Test Facility & Constraints** paragraph at the top of Methods.
   Fill the numbers: 0.36 m², 40 m/s, blockage ratio = (model frontal area / 0.36),
   scale = 1/5, the Reynolds calc you used.
3. **Fill placeholders**: "Xkg" load-cell force, "X" cross-section in your draft.
4. **Write Electronics & DAQ Methods** from your code — describe what each script does
   (sampling rate, unit conversion, live plotting; servo command path & user control).
5. **Write Static Test Procedures** (how you ran each bench test).
6. **Draft Results** from your static data:
   - Load cell: make/insert the calibration plot, report slope, R², resolution, noise band.
   - Servo: commanded vs measured table/plot, note error & repeatability.
7. **Insert CAD figures** (isometric of full model, platform, load-cell housing) with captions.
8. **Move** rolling-wheel/CFD items into the new Future Work section.
9. **Delete leftover template scaffolding** ([Method 1], BOM stubs, [Sample Prep]) so nothing
   reads as unfinished.
10. **Discussion**: 1 paragraph on results, 1 on limitations, 1 on readiness.

## Things you can't finish offline (note them, don't fake them)
- Any plot needing data you don't have yet
- Photos of as-built hardware if not taken
- CFD comparison numbers
