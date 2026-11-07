# Plan programowania – rejestrator 4-kanałowy (NUCLEO-G474RE)

Termin pracy: ~13.12.2026. Płytka z JLC: spodziewana ok. 8–12.11.
Zasada: wszystko, co da się sprawdzić na samej Nucleo, robię **przed** przyjściem płytki.

---

## 0. Narzędzia i założenia

- **STM32CubeIDE** (CubeMX w środku), HAL + LL tam, gdzie liczy się szybkość (ADC/DMA/SPI).
- Nowy projekt dla **NUCLEO-G474RE (STM32G474RET6)**. Stary `STM32/Oscyloskop/Oscyloskop.ioc` jest dla
  STM32G474**CE** z pierwszej wersji – nie używać.
- Zegar: HSE 24 MHz z ST-LINK (bypass) → **SYSCLK 156 MHz**, zegar ADC 52 MHz (PLLP) → 3,467 MSa/s na kanał
  (szczegóły: `cubemx_konfiguracja.md`).
- Debug/komunikacja z PC: **LPUART1 = wirtualny COM ST-LINK (PA2/PA3)** – piny wolne na płytce.
- FFT: **CMSIS-DSP** (`arm_rfft_fast_f32`), G474 ma FPU.
- Git: osobny folder `Firmware/`, commit po każdym działającym kroku.

## 1. Mapa pinów (z netlisty KiCad)

| Funkcja | Pin | Peryferium |
|---|---|---|
| CH1 ADC | PB1 | ADC3 |
| CH2 ADC | PB15 | ADC4 |
| CH3 ADC | PA0 | ADC1 |
| CH4 ADC | PA1 | ADC2 |
| V_BAT (dzielnik 100k/33k) | PC0 | ADC1/ADC2 (kanał wstrzykiwany, rzadko) |
| SPI1 SCK / MISO / MOSI | PA5 / PA6 / PA7 | SPI1 (wyświetlacz + 74HC595) |
| CS 74HC595 (zatrzask RCLK) | PC10 | GPIO |
| CS wyświetlacza | PC4 | GPIO |
| DC/RS wyświetlacza | PC11 | GPIO |
| RESET wyświetlacza | PD2 | GPIO |
| Podświetlenie LED (PWM) | PC7 | TIM8_CH2 |
| 1 kHz kalibracyjny (J13) | PC12 | TIM5_CH2 |
| DC/AC CH1…CH4 | PB13, PB14, PB12, PC6 | GPIO, **1 = DC** (PhotoMOS zwiera C12) |
| Przyciski CH1…CH4 EN | PC1, PB10, PB11, PB0 | GPIO, pull-up zewn., 0 = wciśnięty |
| RUN/STOP, AUTO | PC2, PC3 | GPIO |
| Enkoder T (A/B/przycisk) | PA11 / PA12 / PA10 | TIM4 encoder |
| Enkoder X | PB4 / PB5 / PB9 | TIM3 encoder |
| Enkoder Y | PB6 / PB7 / PA15 | programowo, odczyt co 1 ms (TIM4 zajęty przez T) |

Szczegółowa konfiguracja CubeMX: `cubemx_konfiguracja.md`.

Wzmocnienie – 74HC595 (wysyłka MSB first, zatrzask zboczem PC10):
`bajt = g1 | g2<<2 | g3<<4 | g4<<6`, gdzie g = A1A0 kanału: 0 = G1, 1 = G2, 2 = G5, 3 = G10
(QA = CH1/A0 … QH = CH4/A1).

---

## 2. Etapy

### Etap A – sama Nucleo + wyświetlacz na kabelkach (do przyjścia płytki)

Architektura (pętla główna bez RTOS, przepływ danych, pamięć): `architektura_firmware.md`.
Każdy krok kończy się testem, który da się pokazać, i commitem.

- [ ] **A1. Szkielet.** Poprawki `.ioc` (tabela 11 w `architektura_firmware.md`), `printf` na LPUART1,
  TIM7 co 1 ms, `prof.h` (DWT). Test: w terminalu co 1 s licznik ms i czas pętli w cyklach.
- [x] **A2. Wyświetlacz – sterownik blokujący.** Reset, inicjalizacja ILI9341, odczyt ID (wolne SPI),
  `lcd_fill_rect`, tekst 8×16. Test: kolorowe pasy + napis, czas zamalowania ekranu.
  ✓ 07.11: pełny ekran 159 ms przy SPI /16.
- [x] **A3. Renderer paskowy + DMA SPI.** Siatka 10×8, cztery sztuczne sinusy z tablicy (przesuwające się),
  paski statusu. Test: płynny obraz, klatki/s przy SPI /8 i /4 (tabela w `architektura_firmware.md`).
  ✓ 07.11: 37 kl/s przy SPI 39 MHz (DMA, -O2).
- [x] **A4. Jeden ADC + DMA + DAC.** ADC1 na PA0, wyzwalanie TIM6, DMA. Sygnał: **DAC1 → PA4 (CN7 pin 32)**,
  kabelek do PA0 (CN7 pin 28). Test: sinus z DAC widoczny na LCD – pierwszy „oscyloskop”.
  ✓ 07.11: fs = 3,467 MSa/s potwierdzone (czas 2048 próbek = ideał + stały narzut HAL ~50 cykli, ten sam przy 45 i 48 taktach).
- [x] **A5. Cztery kanały.** ADC1+ADC2 i ADC3+ADC4 w trybie dual (regular simultaneous), wspólny TIM6,
  2 DMA po 32 bity. Test: ten sam sygnał z DAC na PB1 (CN10 24), PB15 (CN10 26), PA0, PA1 (CN7 30) –
  cztery przebiegi bez przesunięcia, zmierzone fs = 3,467 MSa/s, brak zgubionych próbek.
  ✓ 07.11: wszystkie 4 kanały działają; ADC podrzędne (ADC2, ADC4) muszą mieć Overrun = data overwritten.
- [ ] **A6. Wyzwalanie i tryby.** Zbocze, poziom z histerezą, ułamek próbki, AUTO / NORMAL / SINGLE,
  RUN/STOP. Sterowanie: przycisk B1 (PC13) i komendy z UART. Test: stabilny obraz sinusa.
- [ ] **A7. Podstawa czasu.** Tabela T/dz (TIM6 + oversampler), interpolacja przy 1–5 µs/dz.
- [ ] **A8. Podgląd na PC.** `link.c` + `tools/viewer.py`: blok próbek → wykres i CSV.
- [ ] **A9. FFT.** CMSIS-DSP, okno Hann, 1024–4096 punktów; widok FFT na LCD tym samym rendererem.
- [ ] **A10. Tryb szybki 2-kanałowy (opcja).** Przeplot ADC3+ADC1 / ADC4+ADC2 (~6,9 MSa/s) – tylko jeśli
  zostanie czas; inaczej w pracy jako kierunek rozwoju.

### Etap B – uruchomienie płytki

- [ ] **B1. Zasilanie bez Nucleo:** +5V z LDO, pobór prądu z zasilacza z ograniczeniem (~100 mA).
- [ ] **B2. Z Nucleo:** zworka zasilania na **E5V**, sprawdzić 3,3 V, VDDA, 1,625 V (VMID).
- [ ] **B3. Wejście zero:** każdy kanał bez sygnału → ADC ≈ 2048 we wszystkich wzmocnieniach.
- [ ] **B4. 74HC595 + TMUX:** przełączanie G1/G2/G5/G10, sprawdzenie wzmocnienia sygnałem DC.
- [ ] **B5. AC/DC:** PhotoMOS, sygnał z offsetem DC.
- [ ] **B6. Wyświetlacz, enkodery, przyciski** na docelowych pinach.
- [ ] **B7. Kompensacja dzielnika:** prostokąt 1 kHz z J13 na wejście, trymer VC na płaski grzbiet.

### Etap C – funkcje urządzenia

- [ ] **C1. Obsługa z płytki:** enkoder T = podstawa czasu, X = położenie poziome / poziom wyzwolenia,
  Y = V/dz i położenie pionowe, przyciski CH1–CH4, RUN/STOP, AUTO (logika z A6/A7 już gotowa).
- [ ] **C2. Wzmocnienie i AC/DC** z interfejsu (automatyczny dobór G do V/dz), opis zakresu na ekranie.
- [ ] **C3. Tryb roll** dla T/dz ≥ 20 ms (opcja).
- [ ] **C4. Kalibracja:** offset (wejście zwarte, DC) i wzmocnienie (znane napięcie DC) dla każdego
  kanału i zakresu → zapis w ostatniej stronie flash.
- [ ] **C5. Widok FFT** (przełączany), oś w dB, odczyt częstotliwości dominującej.
- [ ] **C6. Pomiary automatyczne:** Vpp, Vśr, Vrms, częstotliwość.
- [ ] **C7. Rejestracja do PC:** tryb blokowy (pełna szybkość, N próbek → UART → CSV) i tryb ciągły
  wolny (np. ≤ 10 kSa/s na kanał, strumień do pliku). Po UART nie da się przesyłać ciągle 3,47 MSa/s.
- [ ] **C8. Pomiar baterii** (V_BAT), ikonka i ostrzeżenie.

### Etap D – pomiary do pracy

- [ ] Pasmo −3 dB każdego zakresu (generator + porównanie z symulacją LTspice).
- [ ] Szum przy zwartym wejściu (LSB rms), ENOB / SFDR z FFT sinusa.
- [ ] Liniowość DC (kilka napięć w całym zakresie), błąd po kalibracji.
- [ ] Rzeczywista częstotliwość próbkowania 4 ch / 2 ch, przesłuchy między kanałami.
- [ ] Zrzuty ekranu i wykresy z rejestracji do pracy.

---

## 3. Moduły kodu

| Plik | Odpowiada za |
|---|---|
| `acq.c` | ADC, DMA, timer próbkowania, bufory, wyzwalanie |
| `afe.c` | 74HC595 (wzmocnienia), AC/DC, przeliczenie kodu ADC → wolty z kalibracją |
| `dsp.c` | FFT, pomiary (Vpp, Vrms, f) |
| `lcd.c` / `gfx.c` | sterownik ILI9341, rysowanie siatki, przebiegów, tekstu |
| `ui.c` | enkodery, przyciski, menu, maszyna stanów (RUN/STOP/AUTO) |
| `link.c` | protokół UART do PC (komendy + wysyłka bloków) |
| `calib.c` | kalibracja i zapis do flash |
| `tools/viewer.py` | podgląd i zapis rejestracji na PC |

Uwaga: SPI1 jest wspólne dla wyświetlacza i 74HC595 – zmiana wzmocnienia tylko gdy nie trwa transfer DMA
do wyświetlacza (np. między klatkami).

## 4. Kalendarz (czas z pomocą Claude: kod i konfiguracje przygotowane, ja testuję na sprzęcie)

Założenie: ~10–12 h tygodniowo na firmware, reszta czasu na pisanie pracy.

| Tydzień | Firmware | h | Praca pisemna | Kamień milowy |
|---|---|---|---|---|
| **27.10–6.11** | CubeMX, projekt wygenerowany | – | rozdział: tor analogowy | |
| **7.11–16.11** | A1 szkielet (2 h), A2 sterownik LCD (3 h), A3 renderer + DMA (4 h), A4 ADC+DAC na LCD (4 h); płytka → B1–B3 (3 h) | ~16 | rozdział: projekt PCB | **sinus z DAC na wyświetlaczu**, płytka żyje |
| **17.11–23.11** | A5 4 kanały (5 h), A6 wyzwalanie (4 h), B4–B7 (4 h + zapas) | ~13 | rozdział: oprogramowanie (architektura) | **3,467 MSa/s × 4 potwierdzone**, przebieg z BNC |
| **24.11–30.11** | A7 podstawa czasu (4 h), C1–C2 obsługa z płytki (4 h), C4 kalibracja (4 h) | ~12 | | urządzenie „jak oscyloskop” |
| **1.12–7.12** | A8 podgląd na PC, A9 FFT, C6 pomiary, C8 bateria (8 h); etap D pomiary (6 h) | ~14 | wyniki pomiarów, wnioski | **zamrożenie funkcji 7.12** |
| **8.12–13.12** | tylko poprawki błędów | ~3 | korekta całości, oddanie | **oddanie pracy** |

Razem firmware: ~60 h (bez A10). A10 (przeplot 2 kanałów) tylko gdy zostanie czas – w pracy może być
opisany jako kierunek rozwoju.
Największe ryzyka czasowe: A5 (dwa DMA przy pełnej szybkości) i B (błędy na płytce). Wyświetlacz jest
teraz na początku, bo od razu służy jako narzędzie do testów ADC (zamiast samego UART).
