# Oscyloskop – plan to the deadline (mid-December 2026)

Written 2026-10-03. Deadline: ~13 Dec 2026 = 10 weeks.
**The thesis text must be finished by W9 (6 Dec); W10 is for corrections only.**

## 0. Reduced scope (decided)

| In | Out (do not touch before the deadline) |
|---|---|
| CH1 only, 1 MΩ input, AC/DC | CH2 (leave unpopulated or delete) |
| 4 ranges: divider ÷1 / ÷33 × gain ×1 / ×5 | ×20 and ×100 gain (DNP footprints only if free) |
| 4 Msps, 12 bit, software trigger, time view + FFT | battery / BMS / USB-C charging (power from bench supply or USB 5 V) |
| TFT 320×240 SPI (ILI9341 class) | hardware trigger, cursors, autoset |

TFT note: the 9-pin socket J2 matches a typical module (SCK, MOSI, MISO, DC, CS, RESET, LED + VCC, GND).
Pick a 320×240 ILI9341 module now; 320×480 needs 2x the frame data.
Check the module's pinout against J2 before the PCB is ordered.

## 1. Proposed requirements (measurable)

| ID | Req | Verified by |
|---|---|---|
| A1 | 1 MΩ input, ≤ ~20 pF, AC/DC coupling | LCR meter |
| A2 | Ranges: ±0.33 V, ±1.65 V, ±11 V, ±55 V (limit to ±40 V if relays/resistors are not rated) | DC levels |
| A3 | −3 dB bandwidth ≥ 1 MHz on all four ranges, ±1 dB flat to 500 kHz | LTspice, then generator sweep |
| A4 | ADC pin always stays in 0–3.3 V (also at 2x overdrive) | simulation + bench |
| A5 | Divider compensated, square-wave overshoot/droop ≤ 3 % | `.tran`, bench |
| D1 | 170 MHz PLL, ADC ~60 MHz, 4 Msps timer-triggered + DMA | scope/generator |
| D2 | ≥ 8k samples, edge trigger, Run/Stop/Auto | demo |
| D3 | 320×240 waveform, grid, V/div, T/div | demo |
| D4 | FFT 1024/2048 pts, Hann, dB magnitude + peak frequency | known sine |
| V1–V5 | DC linearity, frequency response, THD/SFDR/ENOB, noise floor, timebase error | see section 3 |

Targets to claim in the thesis (adjust after simulation): gain error ≤ ±2 % FS, ENOB ≳ 9 bit at 100 kHz.
Not meeting a target is acceptable if you measure it and explain why.

## 2. Schedule (relative to today, 10 weeks)

| Week | Dates | Hardware / simulation | Firmware (on Nucleo-G474RE) | Thesis |
|---|---|---|---|---|
| W1 | 5–11 Oct | **Simulation 1–3** (below). Decide A-requirements | Fix clocks, timer + ADC + DMA at 4 Msps | Chapter 1 intro, aim, scope |
| W2 | 12–18 Oct | **Simulation 4–6**. Freeze analog design | Trigger, UART streaming to PC | Literature review (analog front end, ADC theory) |
| W3 | 19–25 Oct | Annotate schematic, footprints, ERC, BOM | TFT driver | Simulation chapter draft |
| W4 | 26 Oct–1 Nov | **PCB layout, DRC, order boards + parts by 1 Nov** | FFT | Design chapter draft |
| W5 | 2–8 Nov | Wait for boards (1–2 weeks). Prepare measurement setup and scripts | UI, encoders, range control | Firmware chapter draft |
| W6 | 9–15 Nov | Assemble + power-up | Port firmware to PCB | |
| W7 | 16–22 Nov | Bring-up of analog path | Debug | Hardware chapter |
| W8 | 23–29 Nov | **Validation V1–V5** | | Results chapter |
| W9 | 30 Nov–6 Dec | Re-measure if needed | Freeze firmware | Conclusions, abstract, **full draft to supervisor** |
| W10 | 7–13 Dec | Reserve | Reserve | Corrections, print/submit |

**The PCB order on 1 Nov is the critical date.** Everything else slips around it. If simulation fails the A3/A4/A5
checks, shrink the scope (fewer ranges, lower bandwidth claim) rather than delay the order.

**Fallback if the PCB is late or broken:** the thesis can still be completed from the Nucleo plus a breadboard/perfboard
analog front end for V1–V5. Keep that setup alive, do not tear it down.

## 3. MUST DO NOW – simulation (weeks 1–2)

Work in `Symulacje/`. One `.asc` per question. Make every value a `.param` so one number updates everything.
Put each range (÷1/÷33 × ×1/×5) into `.step param range list 1 2 3 4` or four files.
**First:** bring the sim and the KiCad `analog_input` sheet to the same component values and write down which one is the source of truth (right now they differ: 110 p / 3.4 p / 10 p in the sim vs 100 p / 150 p / 3.4 p etc. in KiCad).
Use a real source: 50 Ω or 1 MΩ source resistor, not an ideal source. Add pin capacitance (~2–5 pF) and the 1 MΩ load of a probe if relevant.

| # | Simulation | Directive | What to record (thesis figure/table) |
|---|---|---|---|
| S1 | Bandwidth per range | `.ac dec 100 10 50Meg`, source `AC 1`, plot `dB(V(out)/V(in))` | −3 dB frequency and passband flatness per range → table. Use `.meas AC bw WHEN mag(V(out))=... FALL=1` |
| S2 | Divider compensation | `.tran` 1 kHz square, ÷33; also `.step` C_trim ±30 % | overshoot/droop %, plot with under/over/correct compensation |
| S3 | Large signal, 1 MHz | `.tran 0 20u 0 1n`, 1 MHz sine at max input of each range | clipping, slew limit, distortion (`.four 1Meg V(out)`) |
| S4 | ADC input model | add S&H model: series R (~100–1k Ω from datasheet) + ~5–10 pF to a switch/pulse source at the sample instant | settling error in LSB within 2.5 sample cycles at 60 MHz ADC clock |
| S5 | Overdrive/protection | `.tran` with 50 V step, 2x full-scale input, relay switching | ADC pin voltage stays in 0–3.3 V, diode peak current |
| S6 | Noise | `.noise V(out) V1 dec 100 10 10Meg` | integrated RMS noise referred to ADC vs 1 LSB (806 µV at 3.3 V/12 bit) |

Make a one-row-per-run results table (CSV or markdown). Save plots as images **with the `.asc` that produced them**.
Do not trust the LMH6642 model blindly: compare its S1 result with the datasheet's gain/bandwidth curve and note any difference.

**Exit criterion for W2 (go/no-go for the PCB):** A3, A4, A5 pass in S1, S2, S5 for the four ranges.
If the ÷1 ×5 range does not reach 1 MHz, state a lower bandwidth for that range, don't redesign.

## 4. MUST DO NOW – research / reading (weeks 1–2, then ongoing)

Read in this order, taking notes in your own words (they become the literature chapter):

1. **EEVblog "Advices with DSO input stage design"** – attenuator topology, compensation, protection diodes, buffers.
   https://www.eevblog.com/forum/projects/advices-with-dso-input-stage-design/
   Follow the references it gives: Tektronix 7A-series schematics (TekWiki), *The Art and Science of Analog Circuit Design* chapter "Signal Conditioning in Oscilloscopes", patent US4181903.
2. **EEVblog Project Yaigol / Rigol DS1052E teardown** – real front-end topology.
   https://www.eevblog.com/forum/projects/project-yaigol-fixing-rigol-scope-design-problems
3. **TI reference designs** TIDA-00826 and TIDA-010133 – architecture only (they are far faster than your scope).
4. **ST: AN for OPAMP/ADC on STM32G4** (DM00605707) and the **STM32G474 reference manual** chapter on ADC (sampling time, clock, interleaved mode, DMA) + datasheet (ADC input impedance, S&H capacitance, max clock).
5. **LMH6642 datasheet** (in `Dokumentacja/`) – gain-bandwidth, slew rate, output swing at 5 V, load limits.
6. **ADC theory for the research part**: definitions of gain error, offset, INL/DNL, SNR, THD, SFDR, **ENOB**; sine-fit method (IEEE Std 1241 / 1057 – find the summary your university library provides).
7. **FFT**: windowing (Hann), leakage, bin width = fs/N, CMSIS-DSP FFT usage.

Literature-review outline for the thesis (write one paragraph per bullet as you read):
- how a DSO is built: probe → attenuator → buffer → gain → ADC driver → ADC
- compensated attenuator theory (R1C1 = R2C2, why capacitance matters)
- input protection and relay switching
- ADC driving: source impedance, charge kickback, anti-alias/RC filter
- ADC performance metrics and how they are measured
- FFT basics and spectrum display

## 5. MUST DO NOW – research experiment plan (design it in W2, run it in W8)

Research aspect: **accuracy and linearity of the A/D conversion versus input parameters.**
Fix the design of the experiment now so the PCB provides the test points you need.

| ID | Measurement | Variables | Equipment | Output |
|---|---|---|---|---|
| V1 | DC linearity / gain error | input level, 10+ points per range, both coupling modes | bench supply or calibrator + DMM | error vs level per range; fit gain/offset; INL |
| V2 | Frequency response | 1 kHz–1 MHz, constant amplitude, per range | function generator (check its own flatness) | magnitude vs frequency, vs simulation S1 |
| V3 | Dynamic quality | 1, 10, 100, 500 kHz sine at 3 amplitudes | generator | THD, SFDR, ENOB (sine fit and FFT) |
| V4 | Noise floor | inputs shorted, each range | – | RMS noise in LSB |
| V5 | Timebase error | known-frequency source | generator/frequency counter | ppm error |

Do now:
- Check which instruments you can borrow at the university/lab (DMM class, generator bandwidth and amplitude accuracy). The reference must be better than your device by ~3–10x.
- Write a Python script that reads UART-streamed samples (firmware task D12) and computes V1–V5 automatically.
  Start with simulated data from LTspice `.raw` files so the script is ready when the hardware arrives.
- Plan test points on the PCB: input node, after divider, after gain stage, ADC pin, 3.3 V and 1.65 V rails.
- Decide the uncertainty budget (DMM accuracy, generator accuracy, resistor tolerance) so the thesis can say what an error figure means.

## 6. Thesis skeleton (start writing from W1)

1. Introduction, aim, scope, requirements
2. Theory and state of the art (section 4 outline)
3. Analog front-end design and simulation (S1–S6)
4. Hardware design (schematic, PCB)
5. Firmware (clock/ADC/DMA, trigger, display, FFT)
6. Measurements and results (V1–V5)
7. Conclusions, limitations, future work (CH2, higher gain ranges, hardware trigger)

## 7. Risks (check every Friday)

- Simulation shows LMH6642 too slow → reduce the bandwidth claim, don't redesign.
- PCB order slips past 1 Nov → switch to the fallback (Nucleo + perfboard) immediately.
- TFT refresh too slow → update only the waveform area, lower frame rate, raise SPI clock.
- No reference instruments for V1–V5 → book the lab by W4.
