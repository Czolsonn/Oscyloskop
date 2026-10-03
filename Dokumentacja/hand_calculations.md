# Hand calculations – analog front end (CH1)

Purpose: derive every number by hand BEFORE running LTspice, so the simulation is a check, not a guess.
Each block gives: the equation, what to compute, and a **check value** (my result from the current schematic) to compare with.
Datasheet values are quoted from the PDFs: LMH6642/3/4 (SNOS966Q, in this folder) and OPA354 (SBOS233H, TI).
Book references are by topic (see section 10 for the caveat).

## 0. Circuit and symbols

```
J-in -> [K5: direct | C6 0.1u] -> [K6: signal | ground] -> R9=1M || C8=3.4p -> N
N: R10=60.4k to 3.3 V, R11=63.4k to GND, C9 (variable, ~100p) to GND
N -> U_A OPA354 (unity buffer) -> U5 LMH6642 (+ input)
U5: Rf=R15=1k (out -> -in); Rg (R12=200 / R13=50 / R14=10 ohm) from -in to Vref=1.65 V (second OPA354 buffer)
U5 out -> R16=100 ohm -> ADC pin; C10=150p to GND; BAT54S clamp to GND/3.3 V
```

| Symbol | Meaning | Value |
|---|---|---|
| R_top | R9 | 1 MΩ |
| R_th | R10 ‖ R11 | calculate |
| A | divider ratio | calculate |
| C_top, C_low | C8, (C9 + parasitics + C_in of buffer) | 3.4 pF, calculate |
| V_ref | mid-rail reference | 1.65 V |
| G | gain of U5 | 1 + Rf/Rg |
| V_DDA | ADC supply and reference | 3.3 V |
| LSB | V_DDA / 2^12 | 0.806 mV |

## 1. Divider and compensation

```
R_th = R10*R11/(R10+R11)                                 (1.1)
A = R_th / (R9 + R_th)                                   (1.2)   attenuation (DC and, if compensated, all f)
V_FS,in = (V_DDA/2) / (A*G)                              (1.3)   full-scale input amplitude
C_low,total = R9*C8 / R_th                               (1.4)   compensation condition (R_top*C_top = R_th*C_low)
C_9 = C_low,total - C_in(buffer) - C_stray               (1.5)
tau_div = R9*C8                                          (1.6)
f_p,uncomp = 1 / (2*pi*(R9 || R_th)*C_low)               (1.7)   pole if C8 were missing
C_in,total ~= C8*C_low/(C8 + C_low) + C_stray,in         (1.8)   capacitance seen at the BNC
```
Compensation error: if R9*C8 != R_th*C_low then the gain at HF is `A_hf = C_top/(C_top+C_low)`
versus `A_dc = R_th/(R_top+R_th)`; the relative flatness error is `A_hf/A_dc - 1`. Compute it for a +-5 % error of C_low.

**Check values:** R_th = 30.93 kΩ; A = 0.03000 (÷33.33); V_FS,in(G=1) = 55.0 V; C_low,total = 109.9 pF; f_p,uncomp = 48.2 kHz;
tau_div = 3.4 µs; C_in(series) = 3.30 pF.
Datasheet input: OPA354 C_in = 2 pF (diff) ‖ 2 pF (CM), R_in = 10^13 Ω (p.7).

**Voltage rating:** V_C8 ~= V_in * R9/(R9+R_th) ~= 0.97*V_in (DC) -> C8 and R9 must be rated above the largest input.

## 2. AC coupling

```
R_ac = R9 + R_th                                         (2.1)   resistance seen by C6 in AC mode
f_c = 1 / (2*pi*R_ac*C6)                                 (2.2)
|H(f)| = 1 / sqrt(1 + (f_c/f)^2)       error = 1 - |H|   (2.3)
t_settle(1 %) = 4.6 * R_ac * C6                          (2.4)
C6 = 1 / (2*pi*R_ac*f_c,target)                          (2.5)   design equation
```
**Check values:** R_ac = 1.031 MΩ; f_c = 1.54 Hz; tau = 0.103 s; error 1.2 % at 10 Hz, 0.05 % at 50 Hz, 46 % at 1 Hz;
t_settle(1 %) = 0.47 s. Choose your f_c target (e.g. 2 Hz) and compute C6 with (2.5).

## 3. DC bias, offset and its multiplication by the gain

```
V_th = V_DDA * R11/(R10+R11)                             (3.1)   Thevenin source of the bias network
V_N,AC = V_th                                            (3.2)   AC mode: capacitor blocks the 1 MΩ
V_N,DC(Vin=0) = V_th*(1-A) + A*Vin                       (3.3)   DC mode
V_out = V_ref + G*(V_N - V_ref)                          (3.4)
dV_out = G*(dV_N + V_OS,A + V_OS,5) + (G-1)*V_OS,ref + Rf*I_B,5          (3.5)   worst case: add magnitudes
1/R10 = 1/R11 + 1/R9                                      (3.6)   N = V_DDA/2 in DC mode with grounded input (KCL at N)
```
**Check values:** V_th = 1.690 V; V_N,DC = 1.639 V (−10.7 mV); R10_ideal = 59.62 kΩ; V_N,AC − 1.65 = +40 mV.
Output offset (DC mode) for G = 6, 21, 101 is −64 mV, −225 mV, −1.08 V; (AC mode) +240 mV, +0.84 V, +4.0 V (**clips**).
Add datasheet offsets: OPA354 V_OS = ±2 mV typ / ±8 mV max (p.7); LMH6642 V_OS = ±1 mV typ / ±5 mV max (5 V table, p.8).
Tolerance: compute V_N and V_ref for 1 % and 0.1 % resistors (worst case).
Conclusion to write down: the offset is ~mV x G, so the high gains need offset calibration (auto-zero with K6 grounding the input).

## 4. Gain stage (LMH6642 non-inverting amplifier)

```
G = 1 + Rf/Rg                                            (4.1)   Rf = 1 kΩ
Rg = Rf/(G-1)                                            (4.2)   for G = 5: 250 Ω, for 20: 52.6 Ω, for 100: 10.1 Ω
dG/G = -(Rg_contact/Rg) * (G-1)/G                        (4.3)   effect of relay contact resistance in series with Rg
f_-3dB ~= GBW / G                                        (4.4)   single-pole model
|error(f)| = 1 - 1/sqrt(1 + (f/f_-3dB)^2)                 (4.5)   magnitude error at f
SR_needed = 2*pi*f*V_pk                                  (4.6)
f_FP = SR / (2*pi*V_pk)                                  (4.7)   full-power bandwidth
Cf = sqrt( C_IN / (2*pi*GBW*Rf) )                         (4.8)   LMH6642 datasheet eq. (p.23), 45° phase margin
```
**Check values (labels vs. actual gain):** with Rf = 1 k the stages have G = 1, **6**, **21**, **101** (labelled 1/5/20/100).
Datasheet: GBWP ≈ 57 MHz (text p.23); −3 dB BW 120 MHz at G=+1, 46 MHz at G=+2 (p.8); SR 125 V/µs typ (p.8).

| G | f−3dB = 57 MHz/G | error at 1 MHz | f−3dB (OPA354, 100 MHz/G) |
|---|---|---|---|
| 1 | n/a (120 MHz) | ~0 | |
| 6 | 9.5 MHz | 0.55 % (0.05 dB) | 16.7 MHz |
| 21 | 2.7 MHz | **6.2 %** (0.55 dB) | 4.8 MHz |
| 101 | **0.56 MHz** | **51 %** | 1.0 MHz |

So the ×100 range can NOT reach 1 MHz with the LMH6642 (and barely with an OPA354). ×21 is marginal. SR needed at 1 MHz, 1.65 V amplitude: 10.4 V/µs << 125 V/µs: fine.
Cf check with C_IN = 5 pF: 3.7 pF (datasheet example: 4.1 pF for 6 pF). Mind that at G = 1, Rg is absent: re-check stability.

## 5. Output stage and ADC drive

```
f_RC = 1/(2*pi*R16*C10)                                  (5.1)   = 10.6 MHz  (tau = 15 ns)
Q_kick: dV = V_step * C_ADC/(C10 + C_ADC)                (5.2)   charge sharing when the S&H capacitor is connected
settling: err = exp(-t_s/tau)                            (5.3)   t_s = N_cycles/f_ADC
R_AIN,max = T_s / (f_ADC * C_ADC * ln(2^(N+2))) - R_ADC  (5.4)   ST formula (take C_ADC, R_ADC from the STM32G474 datasheet)
f_s = f_ADC / (T_s + 12.5)                               (5.5)   12-bit; 60 MHz and T_s = 2.5 -> 4 Msps
```
Assuming C_ADC ≈ 5 pF (placeholder – **look it up**): R_AIN,max ≈ 859 Ω − R_ADC. Your R16 = 100 Ω passes as long as R_ADC is small.
With t_s = 2.5/60 MHz = 41.7 ns and tau = 15 ns, only e^(−2.8) = 6 % of the kicked-back charge has decayed – compute the resulting error in LSB.
Output loading: OPA354 closed-loop Z_out = R_O/(1+A·β) ≈ 39 Ω/|A(1 MHz)| ≈ 0.4 Ω at 1 MHz (datasheet R_O = 39 Ω; closed-loop 0.05 Ω below 100 kHz, p.8).

## 6. Noise budget

```
e_R = sqrt(4*k*T*R)                                      (6.1)   22.6 nV/√Hz for R_th = 30.9 kΩ at 300 K
e_n,tot = sqrt( e_R^2 + e_n,A^2 )                         (6.2)   at node N (OPA354 e_n = 6.5 nV/√Hz at 1 MHz)
e_out = G * sqrt( e_n,tot^2 + e_n,LMH^2 + (i_n*(Rf||Rg))^2 + 4kT*(Rf||Rg) )   (6.3)
v_rms = e * sqrt(f_NB),   f_NB = (π/2)*f_-3dB            (6.4)   noise bandwidth of a single-pole system
v_in,rms = v_out,rms / (A*G)                             (6.5)
LSB_in = LSB / (A*G)                                     (6.6)
```
LMH6642: e_n = 17 nV/√Hz at 100 kHz (48 nV/√Hz at 1 kHz), i_n = 0.9 pA/√Hz (p.8). Compute at G = 1, 6, 21 how many LSB rms the noise gives, and the input-referred noise.
The divider's thermal noise (22.6 nV/√Hz) is band-limited by the compensation capacitors (kT/C, about 6 µV rms in total at node N), so in the MHz bandwidth the op-amps (OPA354 6.5, LMH6642 17 nV/√Hz) dominate.
Aliasing: noise bandwidth up to f_-3dB (several MHz) is folded into 0–2 MHz (fs = 4 Msps), so the in-band noise is larger than e·√(1 MHz): compute with f_NB, then compare with the same with an RC filter before the ADC.

## 7. ADC, quantization and FFT (Zieliński)

```
LSB = V_FS/2^N = 0.806 mV                                (7.1)
SNR_q = 6.02*N + 1.76 dB                                 (7.2)   74 dB for N = 12 (full-scale sine)
SINAD = -20 log10( sqrt(10^(-SNR/10) + 10^(-THD/10) + 10^(-SNR_noise/10)) )     (7.3)  combine contributions
ENOB = (SINAD - 1.76)/6.02                               (7.4)
SNR_jitter = -20 log10(2*pi*f_in*t_j)                    (7.5)   t_j = 100 ps at 1 MHz -> 64 dB (!)
Nyquist: f_in < f_s/2 ; at 4 Msps and f_in = 1 MHz only 4 samples per period      (7.6)
Df_bin = f_s/N_FFT                                       (7.7)   4 Msps / 2048 = 1.95 kHz
```
Also compute: samples per period at 100 kHz and 1 MHz; window main-lobe width (Hann: 4 bins); spectral leakage when f_in is not an integer multiple of Df_bin; the processing gain of the FFT `10 log10(N/2)`.

## 8. Supply sensitivity (see answer to question 6)

```
dCode = -Code_dev * dV_DDA / V_DDA                       (8.1)   ratiometric case: error ~ distance from mid-scale
dCode = -2048 * dV_DDA / V_DDA = -0.62 LSB per mV        (8.2)   mid-rail does not track the reference
V_out,err = dV_5V / PSRR(f)                              (8.3)   op-amp supply ripple at the output
```
Datasheet PSRR: LMH6642 +PSRR 79 min / 90 dB typ (p.9, DC); OPA354 ±200 µV/V typ (≈ −74 dB, p.7). Both fall with frequency (see typical curves).
Compute: allowed dV_DDA for 0.5 LSB; the ripple you need at the op-amp supply for 0.1 LSB at G = 21.
RC corner of the reference divider: `f = 1/(2*pi*(R/2)*C)`; for 10k/10k with 10 µF: 3.2 Hz.

## 9. Do the op-amps suit the "low currents" and 1 MHz?

| Requirement | OPA354 (buffers) | LMH6642 (gain stage) | Verdict |
|---|---|---|---|
| Input bias current vs. 30.9 kΩ source | 3 pA typ → 0.09 µV | **1.7 µA** typ → **52 mV** | the buffer in front of the LMH is required |
| Bandwidth at 1 MHz (G = 1) | 250 MHz | 120 MHz | OK |
| GBW | 100 MHz (G=10) | ≈ 57 MHz | G=6 OK; G=21 marginal; G=101 fails |
| Slew rate | 150 V/µs | 125 V/µs | OK (need 10 V/µs) |
| Input CM range with V_s = 5 V | −0.1…5.1 V | −0.1…~3.8 V | 1.65 V mid-rail OK for both |
| Output swing | 0.1–0.3 V from rails | 25–100 mV low, 4.9 V high | ADC 0–3.3 V fits |
| Output current / drive | 100 mA, Z_out 0.05 Ω | ±70 mA | fine for Rg = 10 Ω |
| Noise | 6.5 nV/√Hz | 17 nV/√Hz | noise is dominated by the divider R_th |
| Offset | ±2 mV typ (±8 max) | ±1 mV typ (±5 max) | needs calibration at high gain |
| Supply current | 5 mA | 2.7 mA typ | negligible for the budget (about 13 mA total) |

Open question for the thesis: replace the LMH6642 by an OPA354 as the gain stage? Its GBW is 100 MHz and its input bias is 3 pA, but its
closed-loop bandwidth at G=21 (4.8 MHz) and G=101 (1.0 MHz) is still only just enough. Compute both options in your table.

## 10. Sources

**Datasheets (read these first, numbers are used above):**
- LMH6642/3/4, SNOS966Q: `Dokumentacja/lmh6643.pdf` (family datasheet) – used in sections 5 and 7 above.6 (5 V electrical characteristics), 9.2 (feedback capacitor Cf, GBWP ≈ 57 MHz), 10 (supply decoupling).
- OPA354, SBOS233H: section 6.7 (electrical characteristics), section 8 (application and layout).
- STM32G474 datasheet: ADC characteristics table (C_ADC, R_ADC, f_ADC max, sampling times) and reference manual RM0440, ADC chapter.

**Books you named.** I can't open them from here, so I cannot give page numbers, and the Polish edition's chapter numbers may differ from the English ones I know.
Look these topics up in the table of contents/index (and tell me the chapter numbers so I can pin them in this file):

[2] Horowitz, Hill, *Sztuka elektroniki* (English: *The Art of Electronics*, 3rd ed.):
- Foundations chapter: voltage dividers, Thevenin equivalent, RC high-pass/low-pass filters, capacitive loading, compensated attenuator (kompensowany dzielnik / sonda oscyloskopowa) – used in sections 1 and 2 above.
- "Feedback and operational amplifiers": non-inverting amplifier, gain-bandwidth product, input/output impedance, op-amp limitations (slew rate, bias current, offset), stability with capacitive loads, op-amp input protection – used in sections 4 and 5 above.
- "Precision circuits and low-noise techniques": offset and bias-current error, Johnson noise, e_n/i_n, noise bandwidth, source-resistance matching – used in section 6 above.
- "Digital meets analog" (the ADC chapter): sampling, aliasing, S&H and ADC input drive, ENOB, reference noise, anti-aliasing, layout/grounding for converters – used in sections 5 and 7 above.
- Voltage regulators: PSRR, LDO output noise; High-frequency techniques: stray capacitance, layout, 50 Ω vs. high-impedance.

[1] Zieliński, *Cyfrowe przetwarzanie sygnałów. Od teorii do zastosowań*:
- Sampling theorem, aliasing, quantization noise and SNR (6.02N + 1.76 dB), ADC/DAC parameters – used in equations 7.1–7.6.
- DFT/FFT, frequency resolution, windows (Hann), spectral leakage, zero padding – equation 7.7 and the FFT firmware.
- Analog anti-aliasing filters and their role before the ADC.

**For the compensated attenuator** also use the EEVblog thread already in PLAN.md (references the Tektronix 7A-series and *The Art and Science of Analog Circuit Design*).

## 11. Order of work (tick as you go)

- [ ] 1. Eqs 1.1–1.8 → fill A, R_th, C_low, C_in.
- [ ] 2. Eqs 2.1–2.5 → choose C6.
- [ ] 3. Eqs 3.1–3.6 → offsets per mode and per gain; decide R10 and the auto-zero strategy.
- [ ] 4. Eqs 4.1–4.8 → decide the gain set (1/6/21 or re-sized to 1/5/20), and drop or keep ×100.
- [ ] 5. Eqs 5.1–5.5 → ADC drive and settling, look up C_ADC/R_ADC.
- [ ] 6. Eqs 6.1–6.6 → noise in LSB for every range.
- [ ] 7. Eqs 7.1–7.7, 8.1–8.3 → ENOB budget and supply requirements.
- [ ] 8. Write the results as a table in the thesis, THEN compare with LTspice S1–S6.
