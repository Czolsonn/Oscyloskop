# Final simulation of the CH1 analog front end

Date: 2026-10-06. LTspice 24.0.12, models: OPA354 (TI SBOC018C), LMH6642 (TI), `G6K_SPDT.lib` (own relay model).
Supplies are ideal sources (5 V, 3.3 V). The supply chain is tested separately in `Analog Front End_supply.asc`.

## 1. Circuit that was simulated

The master schematic `Analog Front End.asc` with these changes (all three final files contain the same circuit):

| Part (LTspice name) | Master | Final | Why |
|---|---|---|---|
| R10 (ADC series resistor, R16 in KiCad) | 100 Ω | **47 Ω** | ADC settling (−8.4 LSB → −0.1 LSB) |
| R1 (bias pull-up, R10 in KiCad) | 57.6 k to 3.3 V | **R1a 499 Ω + R1 57.6 k, Cb 100 µF** from the junction to ground | filtered bias feed (supply ripple) |
| Rdac + Vdac | – | **511 k** from the DAC voltage to node N | offset trim |
| C5 (compensation trimmer) | 27 pF | **33.7 pF** | re-trimmed, because Rdac loads node N |
| V7 (relay control) | pulse | `{5*kAC}` | kAC = 0 DC coupling, 1 AC coupling |
| B1 | – | signal source in series with V1 | example signals |

Hand check of the divider with the trim resistor: R_low = 57.6 k ‖ 61.9 k ‖ 511 k = 28.19 kΩ;
in DC mode A = (R_low ‖ 976 k)/976 k = 0.02807 (simulated 0.02808); compensation needs
C_low = R9·C_high/R_low = 976 k · 3.45 pF / 28.19 k = 119.4 pF (82 + 33.7 + 3 stray + op-amp input ≈ 120 pF).

## 2. Files

| File | Content |
|---|---|
| `Analog Front End_final_1_signals.asc` | transient, 5 example signals; set `sig` (1…5) and `kAC` (0/1) |
| `Analog Front End_final_2_AC_response.asc` | `.ac`, 4 gains × DC/AC coupling in one run |
| `Analog Front End_final_3_offset_trim.asc` | transient, before/after trim × DC/AC coupling in one run |
| `Final_1_signals.png`, `Final_1_square_edge.png` | figures for simulation 1 |
| `Final_2_AC_response.png` | figure for simulation 2 |
| `Final_3_offset_trim.png` | figure for simulation 3 |
| `data/*.csv`, `data/*.log`, `data/final_1_meas.json` | exported waveforms, LTspice logs, `.meas` results |
| `make_plots.py` | redraws the figures from `data/` |

The same three `.asc` files are also in `Symulacje/` (next to the symbol files), which is where they must be opened to run.

## 3. Simulation 1 – output for five input signals

`Final_1_signals.png`: left column DC coupling, right column AC coupling. Blue = voltage at the ADC pin (left axis, green band =
ADC range 0…3.3 V), red = input voltage (right axis). All sources start at their mean value, so the coupling capacitor is
already charged at t = 0.

| # | Input | Gain | ADC pin, DC coupling (min … max) | ADC pin, AC coupling (min … max) |
|---|---|---|---|---|
| 1 | sine 1 kHz, 2 V amplitude | 20 | 0.520 … 2.780 V | 0.519 … 2.778 V |
| 2 | sine 1 MHz, 40 V amplitude | 1 | 0.527 … 2.773 V | 0.527 … 2.773 V |
| 3 | square 1 kHz, ±40 V, 1 µs edges | 1 | 0.526 … 2.773 V | 0.521 … 2.773 V |
| 4 | logic square 100 kHz, 0…5 V | 10 | 1.652 … 3.070 V | 0.942 … 2.360 V |
| 5 | 12 V DC + 1 V sine 10 kHz | 3 (DC) / 20 (AC) | 2.581 … 2.750 V | 1.099 … 2.228 V |

What each one shows:
1. Small signal at the highest gain: 4 V p-p becomes 2.26 V p-p, centred on 1.65 V.
2. Top of the band, large signal: no slew limiting, 2.246 V p-p for 80 V p-p, i.e. gain 0.02808, the same as at 1 kHz.
3. Square wave: flat tops and no overshoot (`Final_1_square_edge.png`), so the divider is compensated. In AC mode the top droops
   by about 5 mV in 0.5 ms (time constant 0.1 s).
4. Unipolar signal: DC coupling keeps the 2.5 V mean (only the upper half of the range is used), AC coupling removes it.
5. Ripple on a DC voltage: in DC mode the 12 V limits the gain to 3 and the ripple is 0.17 V p-p (209 LSB); in AC mode gain 20
   can be used and the same ripple is 1.13 V p-p (1402 LSB).

Side finding in case 5 (AC mode): the zero line is 13 mV (17 LSB) too high. It is DC leakage across the coupling path:
12 V over 500 MΩ (capacitor insulation 1 GΩ ‖ open relay contact 1 GΩ, both **assumed** in the models) into node N.
With these values it is about 1.4 LSB per volt of blocked DC at G = 20.

## 4. Simulation 2 – frequency response

`Final_2_AC_response.png`: top = gain from the input to the ADC pin, solid = DC coupling, dashed = AC coupling;
bottom = deviation from the 1 kHz value in percent.

| Gain | Gain input → ADC at 1 kHz | Full scale at the input (±1.65 V at the ADC) | Error at 100 kHz | Error at 1 MHz | f(−3 dB) upper | f(−3 dB) lower, AC coupling |
|---|---|---|---|---|---|---|
| 1 | 0.02808 (−31.03 dB) | ±58.8 V | +0.04 % | −0.02 % | 31.4 MHz | 1.58 Hz |
| 3.004 | 0.08433 (−21.48 dB) | ±19.6 V | +0.04 % | −0.03 % | 14.9 MHz | 1.58 Hz |
| 10.09 | 0.2832 (−10.96 dB) | ±5.83 V | +0.04 % | −0.18 % | 8.04 MHz | 1.58 Hz |
| 20.12 | 0.5646 (−4.97 dB) | ±2.92 V | +0.02 % | −1.33 % | 4.84 MHz | 1.58 Hz |

- DC and AC coupling give the same response above about 100 Hz. In AC mode the gain is −1.2 % at 10 Hz (hand calculation: −1.23 %).
- In DC mode the gain below about 3 Hz is 0.4 % higher than at 1 kHz. This is the bias filter: at DC the pull-up is
  499 Ω + 57.6 k, above the filter corner it is 57.6 k to (AC) ground.
- At G = 20 there is a dip of −0.5 % at 214 kHz (the 10 µF on the Vref buffer, see the Monte Carlo README, section 5).
- The gain to the ADC is 5.4 % lower than in the earlier simulations (0.02966 → 0.02808) because of the 511 k trim resistor.

## 5. Simulation 3 – offset and its correction with the DAC

`Final_3_offset_trim.png`: sine 1 kHz, 2 V amplitude, G = 20. Red = without the trim resistor, blue = with 511 k from the
DAC voltage to node N. Dashed lines = mean value (the zero line on the screen).

| Coupling | Trim | Vdac | Node N (mean) | Zero line at the ADC | Error |
|---|---|---|---|---|---|
| DC | before | – | 1.6516 V | 1.732 V | +82 mV = +102 LSB |
| DC | after | 1.576 V | 1.6475 V | 1.650 V | +0.1 mV = +0.1 LSB |
| AC | before | – | 1.7020 V | 2.748 V | +1098 mV = +1363 LSB, peaks reach 3.94 V (outside the ADC range) |
| AC | after | 0.715 V | 1.6474 V | 1.648 V | −1.8 mV = −2.3 LSB (*) |

(*) not a trim error: the sine starts at t = 0, and the high-pass coupling adds a slowly decaying term of about
amplitude · f_c/f = 2 V · 1.58/1000 = 3 mV at the input. The operating-point run gives exactly 1.650 V with 0.715 V.

How the trim voltages were found: operating point at zero input, Vdac varied until V(v_out) = 1.650 V.

| Gain | Vdac, DC coupling | Vdac, AC coupling | Sensitivity dV_out/dVdac (DC / AC) |
|---|---|---|---|
| 1 | 1.586 V | 0.725 V | 0.054 / 0.055 |
| 3 | 1.579 V | 0.718 V | 0.162 / 0.166 |
| 10 | 1.576 V | 0.716 V | 0.543 / 0.559 |
| 20 | 1.576 V | 0.715 V | 1.083 / 1.114 |

One 12-bit DAC step (0.806 mV) moves the zero line by 0.87–0.90 mV at G = 20, i.e. about 1 ADC LSB.
Trim range at node N (Vdac 0…3.3 V): 1.563…1.740 V in DC mode, 1.608…1.791 V in AC mode. In AC mode only 40 mV of downward
range is left below the working point.

## 6. Limitations

- Typical op-amp models at 27 °C; real offsets differ per part (OPA354 up to ±8 mV, LMH6642 up to ±5 mV), so the trim voltages
  have to be found by the firmware (auto-zero), not taken from this table.
- The BAT54S clamp and the ADC sampling switch are not in these files (ADC settling: `plot's/ADC_Input/`).
- The 100 µF bias-filter capacitor is ideal here. Its corner (3.2 Hz) should match the Vref divider corner (10 k ‖ 10 k, 10 µF);
  with 47 µF instead of 100 µF the supply-ripple sensitivity at G = 20 rises to about 4 LSB per mV around 5 Hz.
- Strays (3 pF at N, 0.15 pF across R9, 2 pF at the inverting input) are assumptions; the trimmer value will differ on the PCB.
- KiCad does not contain these values yet.
