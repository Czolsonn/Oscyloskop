# Calculated values – analog front end CH1 (summary tables)

Companion to `hand_calculations.md` (equations). All values are hand/Python calculations, not simulation or measurement.
Status column: **calc** = computed from the equations, **DS** = taken from a datasheet, **assumed** = placeholder to verify.
Design set used: R9 = 976 kΩ, R10 = 57.6 kΩ, R11 = 61.9 kΩ, C8 = 3.4 pF, C6 = 0.1 µF, Rf = 1 kΩ, R16 = 100 Ω, C10 = 150 pF, V_DDA = 3.3 V.
Gain set B (proposed): G = 1, 3, 10, 20. Datasheet values: LMH6642 (SNOS966Q), OPA354 (SBOS233H), STM32G474 (C_IO, C_ADC from your reading).

## 1. Input divider and bias (Thevenin)

| Quantity | Equation | Value | Status |
|---|---|---|---|
| R_th = R10 ‖ R11 | 1.1 | 29.836 kΩ | calc |
| R_in = R9 + R_th | 2.1 | 1.00584 MΩ (+0.58 % vs 1 MΩ) | calc |
| A = R_th / R_in | 1.2 | 0.029663 (÷33.71) | calc |
| V_th = Vcc·R11/(R10+R11) | 3.1 | 1.7094 V | calc |
| V_N0, DC mode, Vin = 0 | 3.3 | 1.6587 V (+8.7 mV vs 1.65 V) | calc |
| V_N, AC mode | 3.2 | 1.7094 V (+59.4 mV vs 1.65 V) | calc |
| Full scale at G = 1 | 1.3 | ±55.6 V | calc |
| R10 for exactly 1.65 V (R9, R11 fixed) | 3.6 | 58.21 kΩ | calc |
| Alternative R10 = 58.3 kΩ (E192) | – | R_in 1.0060 MΩ, A 0.02984, V_N0 1.6487 V (−1.3 mV) | calc |
| Design recipe for R_in = 1 MΩ, A = 0.03: R_th, R9, V_th, R10, R11 | – | 30 kΩ, 970 kΩ, 1.7010 V, 58.2 kΩ, 61.9 kΩ | calc |

## 2. Compensation of the divider

| Quantity | Equation | Value | Status |
|---|---|---|---|
| C_low,total = R9·C8/R_th | 1.4 | 111.2 pF | calc |
| C8 for C_low = 110 pF | 1.4 | 3.36 pF | calc |
| τ = R9·C8 = R_th·C_low | 1.6 | 3.32 µs | calc |
| Pole without C8 (R9‖R_th = 28.95 kΩ) | 1.7 | 49.4 kHz | calc |
| Input capacitance, series C8·C_low | 1.8 | 3.30 pF + strays (expect 6–15 pF) | calc |
| OPA354 C_IN | – | 2 pF diff ‖ 2 pF CM, R_in = 10¹³ Ω | DS |
| R_th thermal noise density | 6.1 | 22.6 nV/√Hz (R_th alone) | calc |
| Divider noise integrated at node N (kT/C, C = C8 + C_low) | – | 6.0 µV rms | calc |

## 3. AC coupling

| Quantity | Equation | Value | Status |
|---|---|---|---|
| f_c = 1/(2π·R_ac·C6), C6 = 0.1 µF | 2.2 | 1.582 Hz | calc |
| τ = R_ac·C6 | – | 0.1006 s | calc |
| Amplitude error at 1 Hz / 10 Hz / 50 Hz | 2.3 | 46.6 % / 1.23 % / 0.05 % | calc |
| Settling to 1 % / 0.1 % | 2.4 | 0.463 s / 0.695 s | calc |
| C6 for f_c = 2 Hz | 2.5 | 79.1 nF (use 82 nF → 1.93 Hz) | calc |
| C6 for ≤ 1 % error at 10 Hz | 2.5 | ≥ 111 nF (150 nF → 1.05 Hz, 0.55 % at 10 Hz) | calc |

## 4. Offsets multiplied by the gain (equation 3.5, V_N error only)

| G | DC mode: G·(+8.7 mV) | AC mode: G·(+59.4 mV) |
|---|---|---|
| 1 | +8.7 mV | +59 mV |
| 3 | +26 mV | +178 mV |
| 10 | +87 mV | +594 mV |
| 20 | +173 mV | **+1.19 V** (most of the 1.65 V headroom) |

Datasheet offsets to add: OPA354 V_OS ±2 mV typ / ±8 mV max; LMH6642 ±1 mV typ / ±5 mV max; LMH6642 bias current 1.7 µA typ → R_f·I_B = 1.7 mV at the output (DS).
Conclusion: auto-zero calibration per mode and per gain is required, especially G = 10 and 20 in AC mode.

## 5. Gain stage (Rf = 1 kΩ), Set B, bandwidth with GBW = 57 MHz (text) and 90 MHz (from the G = +2 point, 46 MHz)

| G target | Rg ideal | Rg E96 | Actual G | Full scale at input | −3 dB (57 / 90 MHz) | Error at 1 MHz (57 / 90) | Relay contact effect (0.1 Ω) | LSB at input |
|---|---|---|---|---|---|---|---|---|
| 1 | open | – | 1 | ±55.6 V | 120 MHz (DS) | 0.02 % | – | 27.2 mV |
| 3 | 500 Ω | 499 Ω | 3.004 | ±18.5 V | 19.0 / 30 MHz | 0.14 % / 0.06 % | 0.013 % | 9.05 mV |
| 10 | 111.1 Ω | 110 Ω | 10.09 | ±5.51 V | 5.6 / 8.9 MHz | 1.53 % / 0.62 % | 0.08 % | 2.69 mV |
| 20 | 52.6 Ω | 52.3 Ω | 20.12 | ±2.76 V | 2.8 / 4.5 MHz | 5.7 % / 2.4 % | 0.18 % | 1.35 mV |

Other sets for comparison (conservative GBW): A = 1, 2, 5, 10 (max error 1.5 % at 1 MHz). C (current labels 1/5/20/100, real G = 1/6/21/101): at G = 101, −3 dB = 0.56 MHz, error at 1 MHz 50 %; contact effect on 10 Ω is 1 %.
Slew rate needed at 1 MHz, 1.65 V amplitude: 10.4 V/µs (LMH6642: 125 V/µs typ, DS). Full-power bandwidth 12.1 MHz.

## 6. Stability and feedback capacitor (C_IN = 5 pF assumed, GBW = 57 MHz)

| G | Rf ‖ Rg | Pole 1/(2π·(Rf‖Rg)·C_IN) | Crossover GBW/G | Phase margin ≈ 90° − atan(f_x/f_p) |
|---|---|---|---|---|
| 1 | 1000 Ω | 31.8 MHz | 57 MHz | ≈ 29° (peaking) |
| 3 | 333 Ω | 95.6 MHz | 19 MHz | ≈ 79° |
| 10 | 100 Ω | 318 MHz | 5.7 MHz | ≈ 89° |
| 20 | 50 Ω | 637 MHz | 2.85 MHz | ≈ 90° |

C_f = √(C_IN/(2π·GBW·R_f)) = 3.7 pF, f_p = 42.6 MHz (datasheet example with 6 pF gives 4.1 pF / 39 MHz ✓). Place C_f across R15 (inverting node to output side), C0G, with a spare footprint. **assumed** C_IN – tune by simulation.

## 7. Output stage and ADC input (equation section 5)

| Quantity | Value | Status |
|---|---|---|
| C_ADC (sample & hold) | 5 pF typ | DS |
| C_IO (pin) | 5 pF typ | DS |
| Clamp BAT54S capacitance | 10 pF | assumed |
| R_ADC | – | **to look up** |
| C_pin = C10 + clamp + C_IO | 165 pF | calc |
| f_RC = 1/(2π·R16·C_pin) | 9.6 MHz (0.54 % error at 1 MHz) | calc |
| τ = R16·(C_pin + C_ADC) | 17.0 ns | calc |
| T_s = 2.5 / 60 MHz | 41.7 ns | calc |
| Kick fraction C_ADC/(C_pin + C_ADC) | 2.9 % | calc |
| Residual e^(−T_s/τ) | 8.6 % | calc |
| Error at sampling instant | 0.25 % of the sample-to-sample step | calc |
| R_AIN,max = 2/(60 MHz·5 pF·ln 2¹⁴) − R_ADC | 687 Ω − R_ADC | calc (formula from memory, verify) |
| Sample rate 60 MHz/(2.5 + 12.5) | 4.0 Msps | calc |

Largest sample-to-sample step of a full-scale sine (V_step = 2·A·sin(π·f/f_s), A = 1.65 V, f_s = 4 Msps), and the resulting error:

| f | V_step,max | Error (0.25 %) | In LSB |
|---|---|---|---|
| 100 kHz | 0.26 V | 0.65 mV | 0.8 |
| 500 kHz | 1.26 V | 3.2 mV | 3.9 |
| 1 MHz | 2.33 V | 5.9 mV | 7.3 |
| 2 MHz | 3.30 V | 8.4 mV | 10.4 |

## 8. Noise at the ADC (white noise only, existing R16/C10 filter at 9.6 MHz)

Input-referred density at the gain stage: OPA354 6.5 + LMH6642 17 + (G−1)/G · reference buffer 6.5 + Rf/Rg thermal and i_n·R terms; LMH6642 bandwidth taken as 90 MHz/G.

| G | Density | Noise BW | σ at ADC | σ (LSB) | + quantization | SNR (full-scale sine) | ENOB (noise only) | Input-referred σ | LSB at input |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 18.2 nV/√Hz | 14.0 MHz | 0.068 mV | 0.08 | 0.30 | 73.6 dB | 11.9 | 2.3 mV | 27.2 mV |
| 3 | 18.9 | 11.4 MHz | 0.19 mV | 0.24 | 0.37 | 71.7 dB | 11.6 | 2.2 mV | 9.1 mV |
| 10 | 19.2 | 7.3 MHz | 0.52 mV | 0.65 | 0.71 | 66.2 dB | 10.7 | 1.8 mV | 2.7 mV |
| 20 | 19.2 | 4.8 MHz | 0.85 mV | 1.06 | 1.10 | 62.4 dB | 10.1 | 1.4 mV | 1.4 mV |

With an extra 4 MHz filter: G = 10 → 0.50 LSB, G = 20 → 0.88 LSB (not worth the 3 % amplitude error at 1 MHz).
**Not included:** the STM32 ADC's own noise and distortion (look up its SNR/ENOB in the datasheet), supply/reference noise (table 9), charge-kick error (table 7).

## 9. ADC, jitter, FFT

| Quantity | Value | Status |
|---|---|---|
| LSB at the ADC (3.3 V / 4096) | 0.806 mV | calc |
| Quantization SNR, 12 bit | 74.0 dB | calc |
| Quantization noise | 0.289 LSB rms | calc |
| Jitter SNR, 100 ps at 1 MHz / at 100 kHz | 64.0 dB / 84.0 dB | calc |
| Jitter for 74 dB at 1 MHz / for 66 dB | < 32 ps / < 80 ps | calc |
| Samples per period at 1 MHz | 4 | calc |
| FFT bin width, 2048 pts / 1024 pts | 1.95 kHz / 3.9 kHz | calc |
| FFT noise-floor offset 10·log10(N/2), N = 2048 | 30 dB | calc |
| Hann window effective noise bandwidth | 1.5 bins | known value |

## 10. Supply sensitivity

| Quantity | Value | Status |
|---|---|---|
| Error from V_DDA ripple not tracked by the mid-rail | 0.62 LSB per mV | calc |
| Slow drift of V_DDA | 1 % → 1 % gain error | calc |
| LMH6642 PSRR (DC) | 79 dB min / 90 dB typ | DS |
| OPA354 PSRR (DC) | ±200 µV/V typ (≈ 74 dB) | DS |
| Reference divider RC corner (10k ‖ 10k, 10 µF) | 3.2 Hz | calc |

## 11. Op-amp datasheet values used

| Parameter | OPA354 | LMH6642 (V_s = 5 V) |
|---|---|---|
| GBW | 100 MHz (G = 10) | ≈ 57 MHz (app note text) |
| −3 dB bandwidth | 250 MHz (G = 1), 90 MHz (G = 2) | 120 MHz (G = 1), 46 MHz (G = 2) |
| Slew rate | 150 V/µs | 125 V/µs typ |
| Input bias current | 3 pA | 1.7 µA typ |
| Input offset | ±2 mV typ, ±8 mV max | ±1 mV typ, ±5 mV max |
| Voltage noise | 6.5 nV/√Hz (1 MHz) | 17 nV/√Hz (100 kHz), 48 nV/√Hz (1 kHz) |
| Current noise | 300 fA/√Hz | 0.9 pA/√Hz |
| Input capacitance | 2 pF ‖ 2 pF | 2 pF |
| Output | 0.1–0.3 V from rails, 100 mA, R_O 39 Ω | 4.9 V high / 25–100 mV low, ±70 mA |
| Supply current | 5 mA | 2.7 mA typ |

## 12. Open items

- R_ADC and ADC input table for the fast channels (STM32G474 datasheet); STM32 ADC SNR/ENOB.
- BAT54S capacitance and leakage; K3/K7/K8 relay contact and stray capacitance (G6K-2 datasheet).
- Real C_IN and GBW (simulation S1); stability at G = 1 and the value of C_f.
- Decision on the gain set (B recommended) and on the offset-calibration strategy.
- Clock tree: 60 MHz ADC clock from 170 MHz is not an integer division; plan HCLK 120 MHz or a suitable PLLP.
