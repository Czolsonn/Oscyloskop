# Monte Carlo analysis of the CH1 analog front end (simulation "A")

Date: 2026-10-04. LTspice 24.0.12, models: OPA354 (TI SBOC018C), LMH6642 (TI), `G6K_SPDT.lib` (own relay model).
Purpose: how much do component tolerances change the **gain**, the **flatness up to 1 MHz** and the **DC levels**,
and what does that mean for the choice of resistor/capacitor grades and for the trim/calibration strategy.

## 1. What was simulated

Each case is 30 random runs (`.step param run 1 30 1`, values drawn with LTspice `mc(value, tol)`, uniform distribution).
Only passive components are varied. The op-amp models, temperature (27 °C) and supply voltages are nominal.

| Set | Resistors (R4/R9, R1, R5, R2, R3, R6, Rg) | Capacitors (C2, C3, C5, C8) | Strays (C6, C7, C9) |
|---|---|---|---|
| **A** | 0.1 % | 5 % | ±30 % |
| **B** | 1 % | 5 % | ±30 % |
| **C** | 0.1 % | 1 % | ±10 % |

(C4, the 0.1 µF coupling capacitor, is ±10 % in all sets. The gain-stage Rg is 1G for G = 1 and 52.3 Ω for G = 20.12.)

| Case name | Analysis | Set | Gain | Coupling |
|---|---|---|---|---|
| `AC_A_G1`, `AC_B_G1`, `AC_C_G1` | `.ac dec 20 1k 50Meg` | A, B, C | G = 1 | DC |
| `AC_A_G20` | `.ac` | A | G = 20.12 | DC |
| `OP_A_G1_DC`, `OP_A_G20_DC` | `.dc` (operating point at zero input) | A | 1 / 20.12 | DC |
| `OP_A_G1_ACm`, `OP_A_G20_ACm` | same | A | 1 / 20.12 | AC (relay energized) |

## 2. Files

| File | Content |
|---|---|
| `Analog Front End_MC_AC.asc`, `Analog Front End_MC_OP.asc` | the LTspice schematics (tolerances as `{mc(...)}`, parameters `tolR tolC tolS kAC Rg`) |
| `MC_response_AC_A_G1.png`, `MC_response_AC_A_G20.png` | frequency response of all 30 runs |
| `MC_compare_tolerances_G1.png` | effect of the three tolerance sets on the flatness |
| `MC_histograms_G1.png` | distributions of gain and flatness |
| `MC_dc_bias.png` | DC levels at zero input |
| `MC_summary.csv` | mean, standard deviation, minimum, maximum of every quantity |
| `data/*_meas.csv`, `data/*_response.csv`, `data/OP_*.csv` | per-run results exported from the LTspice logs/raw files |
| `data/netlists/*.cir`, `data/logs/*.log` | exact netlists and LTspice logs of every run |
| `data/Vref_decoupling_dip_G20.csv` | small follow-up study (section 5) |
| `make_plots.py` | regenerates all figures and `MC_summary.csv` from `data/` |

## 3. The plots

**`MC_response_AC_A_G1.png`** – top: gain relative to 1 kHz in dB for all 30 runs (−3 dB at about 11 MHz, set by the output
filter and the op-amp); bottom: zoom up to 3 MHz in percent. The curves fan out between 10 kHz and 100 kHz and then stay
parallel up to 1 MHz: this is the mis-compensation of the input divider (C_low versus C8). The spread is −8 % … +4 %.

**`MC_response_AC_A_G20.png`** – the same for G = 20. The −3 dB point is lower (3.3 … 4.2 MHz), and at 1 MHz the response is
already 4 % below the 1 kHz value on average. All curves have a small notch near 158 kHz (see section 5).

**`MC_compare_tolerances_G1.png`** – min–max band and median for the three sets. Going from set A to B (resistors 0.1 % → 1 %)
changes almost nothing in the flatness; going to set C (capacitors 1 %, strays 10 %) shrinks the band by about 4×.

**`MC_histograms_G1.png`** – left: gain at 1 kHz relative to the hand-calculated 0.029663; the resistor grade is the only thing
that matters (σ = 0.08 % for 0.1 %, σ = 0.75 % for 1 %). Middle/right: gain at 100 kHz and 1 MHz relative to 1 kHz; the capacitor
tolerance is what matters (σ = 2.9–3.8 % for sets A and B, 0.7–0.8 % for set C).

**`MC_dc_bias.png`** – left: the bias node N and the reference Vref at zero input in DC mode; middle: the output offset in DC mode for
G = 1 and G = 20; right: the same in AC mode. The boxes show the spread of the 30 runs (green triangle = mean).

## 4. Results (30 runs per case)

| Quantity | Set A (G=1) | Set B (G=1) | Set C (G=1) | Set A (G=20) |
|---|---|---|---|---|
| Gain at 1 kHz, deviation from nominal | σ 0.08 %, −0.14 … +0.18 % | σ 0.75 %, −1.33 … +1.83 % | σ 0.08 %, −0.13 … +0.18 % | σ 0.11 %, −0.25 … +0.20 % |
| Gain at 100 kHz vs. 1 kHz | σ 2.85 %, −6.4 … +3.8 % | σ 3.10 %, −6.5 … +4.7 % | σ 0.66 %, −1.3 … +1.0 % | σ 2.85 %, −6.5 … +3.7 % |
| Gain at 1 MHz vs. 1 kHz | σ 3.46 %, −8.3 … +4.1 % | σ 3.76 %, −8.3 … +5.2 % | σ 0.80 %, −2.1 … +0.7 % | σ 3.38 %, −10.4 … +1.8 % |
| f(−3 dB) | 10.9 ± 0.9 MHz | 10.9 ± 1.0 MHz | 11.2 ± 0.2 MHz | 3.8 ± 0.2 MHz |

DC levels at zero input (set A, mV above 1.65 V; mean, σ, range):

| Node | DC mode | AC mode |
|---|---|---|
| N | +8.76, 0.47, 7.8 … 9.7 | +59.3, 0.49, 58.4 … 60.2 |
| Vref | +0.96, 0.66, −0.4 … +2.2 | +0.95, 0.66 |
| Output, G = 1 | +10.7, 0.47, 9.8 … 11.6 | +61.3, 0.49 |
| Output, G = 20 | +228, 17.9, 195 … 265 | **+1246**, 18.2, 1212 … 1284 |

The means agree with the hand calculations (N: +8.7 mV in DC mode, +59 mV in AC mode). With 30 runs the standard error of a
mean is σ/√30, i.e. about 0.5 % for the flatness figures, so the flatness means (−1.2 % at 100 kHz) are only roughly
determined; the min–max ranges and σ are the useful numbers.

## 5. Side finding: notch at ~158 kHz at G = 20

All G = 20 curves have the same small dip at about 158 kHz (−0.8 %), independent of the tolerances. It comes from the 10 µF
decoupling capacitor on `Vref` (C13): the output impedance of the OPA354 buffer resonates with it, and with Rg = 52 Ω the
variation of the reference impedance reaches the gain. Follow-up on the nominal circuit (`data/Vref_decoupling_dip_G20.csv`):
without C13 the dip is gone; a series resistance of 0.3–1 Ω in the C13 branch (or a capacitor with higher ESR) reduces the
minimum from −0.82 % to −0.59 … −0.46 %. It is a small effect, but it is real in the model and worth a series resistor on the PCB
(footprint for 0–1 Ω in series with the 10 µF).

## 6. Conclusions for the design

1. **Gain accuracy is set by the resistors.** 0.1 % parts give ±0.2 % at 1 kHz; 1 % parts give ±1.5 %. Use 0.1 % for the divider
   (R4/R9, R1, R5), for Rf and for the gain resistors Rg if a 1 % calibration residual is not acceptable.
2. **Flatness up to 1 MHz is set by the capacitors and the strays, not by the resistors.** With 5 % capacitors the response at
   100 kHz–1 MHz varies by about ±5 % (up to −8 %) and the resistor grade does not help. Two ways out: **trim C5 on every board**
   with the 1 kHz square-wave method (needs the trimmer), and/or use 1 % C0G capacitors with a layout that keeps the strays
   predictable (set C: σ ≈ 0.7 %).
3. **The DC offsets are repeatable.** The spread of N is ±1 mV and of the output ±1 mV (G = 1) or ±20 mV (G = 20), so the planned
   DAC trim range (±100 mV at node N) is more than enough. The mean offset is the problem, not the spread: in AC mode at
   G = 20 the zero line sits at 1.65 V + 1.25 V = 2.9 V, so the trim (or a gain limit in AC mode) is needed.
4. **Bandwidth** at G = 20 is 3.3 … 4.2 MHz, so ≥ 1 MHz is met in every run, with a 4 % average roll-off at 1 MHz that can be
   calibrated in firmware.

## 7. Limitations and notes

- Only 30 runs per case and only passives vary: op-amp offset/GBW spread, temperature and supply tolerances are not included.
- Only G = 1 and G = 20.12 were run (the two extremes of the Set B gains).
- Solver: the DC operating point of the two OPA354 vendor models is fragile, so the schematics use
  `.options noopiter gminsteps=0 abstol=1e-10 reltol=0.003 vntol=1e-5` and a `.nodeset`. The first attempt of `AC_C_G1` aborted
  at step 3 (failed operating point) and was repeated with `gminsteps` enabled; the other cases ran in one go.
- With `.step param run 1 30 1` the DC (`OP`) runs logged only 29 `.meas` rows, so the `OP` schematic uses `run 1 31 1`, which
  gives 30 logged values. The netlists in `data/netlists/` are the ones used.
- Raw `.raw` files are not stored (large, ignored by git). The per-run data is in the CSV files.

## 8. How to reproduce

Open `Analog Front End_MC_AC.asc` (or `_MC_OP.asc`) next to the symbol files (`OPA354.asy`, `LMH6642.asy`, `G6K_SPDT.asy`),
set `.param tolR/tolC/tolS/kAC/Rg`, run, read the results in **View → SPICE Error Log**
(right-click a measurement → *Plot .step'ed .meas data*). To redraw the figures: `python make_plots.py`.
