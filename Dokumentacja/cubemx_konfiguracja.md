# STM32CubeMX – konfiguracja pinów (NUCLEO-G474RE)

Mapowanie funkcji pinów sprawdzone w bazie CubeMX (`db/mcu/STM32G474R(B-C-E)Tx.xml`).

## 0. Nowy projekt

1. **File → New Project → Board Selector** → wpisz `NUCLEO-G474RE` → Start Project.
2. „Initialize all peripherals with their default Mode?” → **Yes**.
   Ustawi się: LPUART1 (PA2/PA3, wirtualny COM ST-LINK), przycisk B1 (PC13), LD2 (PA5), HSE, LSE, SWD.
3. **Od razu kliknij PA5 → Reset_State.** LD2 jest na PA5, a PA5 to u nas SPI1_SCK.
   (LD2 na Nucleo i tak będzie migać przy transmisji SPI – to normalne.)

## 1. Piny – klikasz pin w widoku Pinout i wybierasz funkcję

Najpierw przypisz funkcje **na pinach** (klik na pin), dopiero potem włączaj peryferia po lewej –
inaczej CubeMX sam przeniesie np. TIM3 na PA6/PA7 (zajęte przez SPI).

### Analog
| Pin | Funkcja do wybrania | Etykieta (prawy klik → Enter User Label) |
|---|---|---|
| PB1 | `ADC3_IN1` | CH1 |
| PB15 | `ADC4_IN5` | CH2 |
| PA0 | `ADC1_IN1` | CH3 |
| PA1 | `ADC2_IN2` | CH4 |
| PC0 | `ADC1_IN6` | V_BAT |
| PA4 | `DAC1_OUT1` | DAC_TEST (sygnał testowy, tylko na etapie A) |

### SPI1 (wyświetlacz + 74HC595)
| Pin | Funkcja |
|---|---|
| PA5 | `SPI1_SCK` |
| PA6 | `SPI1_MISO` |
| PA7 | `SPI1_MOSI` |

### Wyjścia GPIO (`GPIO_Output`)
| Pin | Etykieta | Stan początkowy (GPIO output level) |
|---|---|---|
| PC10 | REG_LATCH (74HC595 RCLK) | Low |
| PC4 | LCD_CS | **High** (nieaktywny) |
| PC11 | LCD_DC | Low |
| PD2 | LCD_RST | **High** |
| PB13 | DCAC_CH1 | Low |
| PB14 | DCAC_CH2 | Low |
| PB12 | DCAC_CH3 | Low |
| PC6 | DCAC_CH4 | Low |

DCAC: 1 = DC (PhotoMOS zwiera kondensator), 0 = AC.

### Timery z wyjściem
| Pin | Funkcja | Etykieta |
|---|---|---|
| PC7 | `TIM8_CH2` | LCD_LED (PWM podświetlenia) |
| PC12 | `TIM5_CH2` | CAL_1KHZ (J13) |
| PB4 | `TIM3_CH1` | ENC_X_A |
| PB5 | `TIM3_CH2` | ENC_X_B |
| PA11 | `TIM4_CH1` | ENC_T_A |
| PA12 | `TIM4_CH2` | ENC_T_B |

### Wejścia GPIO (`GPIO_Input`, No pull-up/pull-down – są zewnętrzne 10k)
| Pin | Etykieta |
|---|---|
| PB6 | ENC_Y_A (programowo – TIM4 zajęty przez T) |
| PB7 | ENC_Y_B |
| PA10 | ENC_T_SW |
| PB9 | ENC_X_SW |
| PA15 | ENC_Y_SW |
| PC1 | BTN_CH1 |
| PB10 | BTN_CH2 |
| PB11 | BTN_CH3 |
| PB0 | BTN_CH4 |
| PC2 | BTN_RUN |
| PC3 | BTN_AUTO |

Przyciski i enkoder Y czytamy w przerwaniu co 1 ms (TIM7), bez EXTI – prościej i z odfiltrowaniem drgań.

## 2. Zegar (Clock Configuration)

Cel: **SYSCLK 156 MHz i zegar ADC dokładnie 52 MHz** → 52 MHz / 15 cykli = **3,467 MSa/s**,
a timer próbkowania liczy 45 taktów 156 MHz na próbkę – idealnie równo.

- PLL Source Mux: **HSE** (24 MHz)
- PLLM: **/6** → 4 MHz
- PLLN: **×78** → 312 MHz
- PLLR: **/2** → SYSCLK **156 MHz** (System Clock Mux: PLLCLK)
- PLLP: **/6** → **52 MHz**
- ADC12 Clock Mux i ADC345 Clock Mux: **PLLP**
- AHB, APB1, APB2 prescaler: /1

CubeMX sam włączy „Power Regulator Voltage Scale 1 boost” – zostaw.

## 3. Peryferia (lewa kolumna)

| Peryferium | Ustawienie |
|---|---|
| **SPI1** | Mode: Full-Duplex Master, Hardware NSS: Disable, Data Size 8 bit, MSB First, Prescaler **/16** (9,75 MHz na start; potem przyspieszymy), CPOL Low, CPHA 1 Edge |
| **LPUART1** | Asynchronous, **115200** na start (później 2 Mbaud), 8N1 |
| **TIM3** | Combined Channels: **Encoder Mode**, Encoder Mode: TI1 and TI2, Input Filter: **10** (oba kanały) |
| **TIM4** | jak TIM3 |
| **TIM5** | Channel2: **PWM Generation CH2**, Prescaler **155**, Counter Period **999**, Pulse **500** → 1 kHz, 50 % |
| **TIM8** | Channel2: **PWM Generation CH2**, Prescaler 0, Counter Period **7799** (20 kHz), Pulse 7799 (pełna jasność) |
| **TIM7** | Activated, Prescaler **155**, Counter Period **999** → przerwanie co 1 ms; NVIC: TIM7 global interrupt ✓ |
| **TIM6** | Activated, Trigger Event Selection: **Update Event** (TRGO dla ADC), Prescaler 0, Counter Period **44** (156 MHz / 45 = 3,467 MHz) |
| **ADC1–4, DAC1** | na razie tylko piny z punktu 1 – szczegóły (DMA, tryb dual, wyzwalanie TIM6) w kroku A2/A4 planu |

## 4. Project Manager

- Project Name: `Rejestrator`, Location: `…/Projekty/Oscyloskop/Firmware`
- Toolchain/IDE: **STM32CubeIDE** (jeśli na liście), w przeciwnym razie **CMake** i import w CubeIDE 2.0
  (File → Import → CMake project)
- Code Generator: ✓ *Generate peripheral initialization as a pair of '.c/.h' files per peripheral*,
  ✓ *Set all free pins as analog* (mniej szumu i poboru prądu)
- **GENERATE CODE**

## 5. Sprawdzenie po wygenerowaniu

W `main.c` w pętli:
```c
printf("ok %lu\r\n", HAL_GetTick());   // potrzebny _write() przekierowany na LPUART1
HAL_Delay(500);
```
Terminal (np. PuTTY) na porcie „STMicroelectronics STLink Virtual COM Port”, 115200 – ma się pojawiać tekst.
