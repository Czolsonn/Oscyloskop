# Architektura firmware – rejestrator 4-kanałowy (NUCLEO-G474RE)

Projekt CubeIDE: `Firmware/Digital Time-Frequency Recorder/`.
Mapa pinów i nastawy CubeMX: `cubemx_konfiguracja.md`, etapy i kalendarz: `plan_programowania.md`.

---

## 1. Decyzja: pętla główna + przerwania, bez RTOS

Model **foreground/background** („superloop”):

- **Przerwania (foreground)**: krótkie, tylko ustawiają flagi, wpisują do buforów pierścieniowych lub
  zatrzymują timer. Bez obliczeń, bez `HAL_Delay`, bez `printf`.
- **Pętla główna (background)**: kolejno wywołuje `*_poll()` każdego modułu. Każdy moduł jest małą
  maszyną stanów, która **nigdy nie czeka** – jeśli nie ma nic do zrobienia, od razu wraca.

Dlaczego bez RTOS (do pracy):

1. Twarde wymagania czasowe spełnia sprzęt: timer → ADC → DMA próbkuje bez udziału CPU,
   SPI do wyświetlacza też idzie przez DMA. CPU ma ograniczenia rzędu milisekund (obraz, przyciski).
2. Zadań jest mało i są sekwencyjne (akwizycja → obróbka → rysowanie), nie konkurują ze sobą.
3. Brak RTOS = brak osobnych stosów, priorytetów zadań, muteksów (SPI jest wspólne dla wyświetlacza
   i 74HC595), łatwiejsze debugowanie i mniejsze ryzyko przy krótkim terminie.
4. RTOS miałby sens przy wielu niezależnych, blokujących zadaniach (np. stos USB/TCP, karta SD z systemem
   plików). To jest kierunek rozwoju, nie potrzeba teraz.

Czas pętli i obciążenie CPU mierzymy licznikiem cykli **DWT->CYCCNT** (1 takt = 6,4 ns) – wyniki do pracy.

```c
/* main.c, USER CODE 2 / WHILE */
app_init();
while (1) {
    ui_poll();      /* zdarzenia z przycisków i enkoderów -> zmiana nastaw            */
    app_apply();    /* nastawy zmienione? -> afe (wzmocnienia, AC/DC), acq (podstawa)  */
    acq_poll();     /* maszyna stanów akwizycji, wyszukiwanie wyzwolenia              */
    view_poll();    /* gotowa ramka -> kolumny min/max lub FFT, pomiary               */
    gfx_poll();     /* renderuje kolejny pasek ekranu, gdy poprzedni poszedł przez DMA */
    link_poll();    /* komendy z PC, wysyłka bloków                                   */
    batt_poll();    /* co 1 s pomiar V_BAT                                            */
}
```

## 2. Przepływ danych

```
 CH1..CH4 (płytka)          sprzęt, bez CPU                         pętla główna
 ───────────────     ──────────────────────────────    ───────────────────────────────────────
                     TIM6 TRGO (156 MHz / ARR+1)
                        │ jeden wspólny wyzwalacz
            ┌───────────┴───────────┐
 CH3 PA0 → ADC1 ┐               ADC3 ┐ ← PB1  CH1
 CH4 PA1 → ADC2 ┴ dual → CDR    ADC4 ┴ ← PB15 CH2
                │ 32 bit             │ 32 bit
            DMA1 ch1             DMA1 ch2
                ▼                    ▼
           raw12[N]             raw34[N]           acq: wyzwolenie (indeks + ułamek)
          (CH3|CH4<<16)        (CH1|CH2<<16)  ──►  frame_t {wskaźniki, N, fs, trig}
                                                          │
                                ┌─────────────────────────┼─────────────────────┐
                                ▼                         ▼                     ▼
                     view: kolumny min/max (300)    dsp: FFT, pomiary     link: blok → UART → PC
                                │                         │
                                └──────────┬──────────────┘
                                           ▼
                     gfx: paski 300×10 px → DMA SPI1 → ILI9341
                                           ▲
                     ui: przyciski, enkodery → settings_t → afe (74HC595 przez SPI1, DC/AC GPIO)
```

Zasady:

- **Jedno źródło prawdy: `settings_t`** (wzmocnienia, AC/DC, V/dz, T/dz, wyzwalanie, tryb). UI tylko
  zmienia nastawy i ustawia flagę `dirty`; `app_apply()` przekłada je na sprzęt.
- Próbki zostają w **kodach ADC** aż do końca. Przeliczenie na wolty (z kalibracją) tylko dla wyników
  pomiarów i opisów na ekranie. Do rysowania: kod → piksel jedną mnożną skalą liczoną raz po zmianie nastaw.
- Dane surowe po trybie dual: `uint16_t` z przeplotem, np. `raw34_16[2*i] = CH1`, `raw34_16[2*i+1] = CH2`.

## 3. Akwizycja (`acq.c`)

Tryb blokowy (prosty i wystarczający):

```
IDLE ──RUN──► ARM ──(DMA TC obu par)──► DONE ──szukaj wyzwolenia──┬─ znalezione ──► FRAME ──► ARM
                ▲                                                  ├─ AUTO i >100 ms bez wyzw. ──► FRAME (bez wyzw.)
                └──────────────────────────────────────────────────┘ nie ma ──► ARM
SINGLE: po pierwszej ramce z wyzwoleniem → IDLE (STOP)
```

- ARM: oba ADC dual uzbrojone (`HAL_ADCEx_MultiModeStart_DMA`), **na końcu** start TIM6 – wtedy
  wszystkie 4 ADC zaczynają od tego samego taktu.
- Przerwanie DMA TC: tylko zatrzymanie TIM6 i flaga. Zatrzymanie ADC i reszta w pętli głównej.
- Kalibracja ADC (`HAL_ADCEx_Calibration_Start`, single-ended) raz po starcie.
- Wyzwolenie programowe z histerezą: uzbrojenie, gdy próbka < poziom − h, wyzwolenie przy pierwszej
  próbce ≥ poziom (zbocze opadające – odwrotnie). Szukanie w zakresie `[W/2, N − W/2]`, żeby punkt
  wyzwolenia był na środku ekranu (W = liczba próbek na ekranie).
- **Ułamek próbki**: `t = (poziom − x[i−1]) / (x[i] − x[i−1])` i przesunięcie obrazu o ułamek –
  bez tego przy szybkich podstawach czasu przebieg skacze o ±1 próbkę (±289 ns).
- Długość bloku: `N = max(2·W + zapas, N_FFT)`, maks. 4096 na kanał.
- Ograniczenie (do opisania w pracy): między blokami jest czas martwy (obróbka + rysowanie), więc
  rzadkie zdarzenia można przegapić. Rozszerzenie: DMA cyrkularne + analog watchdog ADC jako wyzwalanie sprzętowe.

### Podstawa czasu (30 px na działkę, 10 działek = 300 px)

Od 10 µs/dz w górę: dokładnie **1 próbka na piksel**, timer TIM6 i nadpróbkowanie (oversampler ADC,
każda konwersja wyzwalana timerem – uśrednianie po całym okresie próbki, działa jak filtr antyaliasingowy).
Liczba taktów na próbkę = 156 MHz · T/dz / 30 = (ARR+1) · OVS.

| T/dz | ARR+1 | OVS | fs wyjściowe | okno |
|---|---|---|---|---|
| 1 / 2 / 5 µs | 45 | ×1 | 3,467 MSa/s | 10–50 µs, **interpolacja** (0,12–0,58 próbki/px) |
| 10 µs | 52 | ×1 | 3 MSa/s | 0,1 ms |
| 20 µs | 52 | ×2 | 1,5 MSa/s | 0,2 ms |
| 50 µs | 65 | ×4 | 600 kSa/s | 0,5 ms |
| 100 µs | 65 | ×8 | 300 kSa/s | 1 ms |
| 200 µs | 65 | ×16 | 150 kSa/s | 2 ms |
| 500 µs | 325 | ×8 | 60 kSa/s | 5 ms |
| 1 ms | 325 | ×16 | 30 kSa/s | 10 ms |
| 2 ms | 325 | ×32 | 15 kSa/s | 20 ms |
| 5 ms | 1625 | ×16 | 6 kSa/s | 50 ms |
| 10 ms | 1625 | ×32 | 3 kSa/s | 100 ms |
| ≥ 20 ms | 1625 / 8125 / 40625 | ×64–128 | ≤ 1,5 kSa/s | ≥ 0,2 s → **tryb roll** (opcja) |

Do sprawdzenia w RM0440 (rozdz. „Oversampler”): czy nadpróbkowanie działa w trybie dual.
Jeśli nie – uśrednianie programowe (przy niskim fs CPU ma na to czas).

## 4. Obraz (`lcd.c`, `gfx.c`, `view.c`)

Bufor całej ramki 320×240×2 B = 150 kB **nie mieści się** w 128 kB RAM. Dlatego:

- **Pole przebiegów 300×200 px** jest rysowane w całości co klatkę, **paskami 300×10 px** (6 kB).
  Dwa bufory paska: CPU rysuje pasek B, a DMA wysyła pasek A. Brak migotania, brak „gumki”.
- Przebieg jest zapisany jako **kolumny min/max**: dla każdego z 300 pikseli i każdego kanału
  `ymin[x], ymax[x]` (5 kB). Renderer w każdym wierszu paska rysuje: tło → siatkę → pionowe odcinki
  kanałów → linię poziomu wyzwolenia. Min/max to od razu detekcja szczytów przy wolnych podstawach czasu.
- **FFT używa tego samego renderera** (kolumna = maks. z prążków przypadających na piksel).
- Paski statusu (góra, dół, prawa kolumna) – tylko po zmianie (flaga `dirty`), małe okna, między klatkami.
- Piksele RGB565 w buforze zapisane od razu w kolejności bajtów dla SPI (`__REV16`) – SPI zostaje
  w trybie 8-bit, więc 74HC595 nie wymaga przestawiania SPI.

Układ ekranu (poziomo, 320×240):

```
 ┌────────────────────────────────────────── 320 ─┐
 │ RUN  T:10µs/dz  Trg:CH1 ↑ 1,20 V   AUTO   ▮▮▯  │ 16 px  pasek statusu
 ├──────────────────────────────────────────┬─────┤
 │                                          │  ◄1 │
 │      siatka 10 × 8 dz (30 × 25 px)       │  ◄T │ 200 px
 │      300 × 200 px                        │  ◄2 │
 ├──────────────────────────────────────────┴─────┤
 │ 1: 2V DC   2: 5V AC   3: off   4: 1V DC  Vpp.. │ 24 px  kanały / pomiary
 └────────────────────────────────────────────────┘
```

Przepustowość SPI dla pola 300×200 (120 kB na klatkę):

| SPI1 | czas klatki | klatki/s (maks.) |
|---|---|---|
| /16 = 9,75 MHz | 98 ms | ~10 |
| /8 = 19,5 MHz | 49 ms | ~20 (start na kabelkach) |
| /4 = 39 MHz | 25 ms | ~40 (docelowo, jeśli moduł pozwala) |

### Skala pionowa – automatyczny dobór wzmocnienia

Ekran ±4 dz. Wybierane jest **największe wzmocnienie, którego zakres pokrywa ekran**; mniejsze V/dz to
już powiększenie cyfrowe. Zakresy pełne (z `wartosci_AFE_4kanaly.md`): G1 ±34,3 V, G2 ±17,2 V,
G5 ±6,9 V, G10 ±3,5 V.

| V/dz | ekran | wzmocnienie | kody/dz |
|---|---|---|---|
| 10 V | ±40 V | G1 | 597 |
| 5 V | ±20 V | G1 | 299 |
| 2 V | ±8 V | G2 | 238 |
| 1 V | ±4 V | G5 | 296 |
| 0,5 V | ±2 V | G10 | 292 |
| 0,2 V | ±0,8 V | G10 | 117 |

## 5. Wejścia użytkownika (`ui.c`)

- Enkodery T i X: liczniki TIM4/TIM3 w trybie enkodera, w `ui_poll()` odczyt różnicy licznika.
- Enkoder Y i wszystkie przyciski: przerwanie **TIM7 co 1 ms** – tablica stanów enkodera i filtr drgań
  (stan stabilny przez 20 ms). Wynik → **kolejka zdarzeń** (bufor pierścieniowy), np. `EV_BTN_RUN`, `EV_ENC_T(+1)`.
- `ui_poll()` zdejmuje zdarzenia i zmienia `settings_t`. Na etapie samej Nucleo: przycisk B1 (PC13) i
  komendy z UART zastępują przyciski płytki.

## 6. Wspólne SPI1 (wyświetlacz + 74HC595)

`afe_set_gain()` tylko zapamiętuje nowy bajt. Wysyłka do 74HC595 (+ impuls na PC10) następuje w
`gfx_poll()`, gdy DMA paska jest wolne (między paskami lub klatkami). Nigdy dwóch użytkowników SPI naraz.
Po zmianie wzmocnienia lub AC/DC odrzucamy bieżący blok (stan przejściowy wzmacniacza i PhotoMOS).

## 7. Komunikacja z PC (`link.c`)

- LPUART1 = wirtualny COM ST-LINK. RX w przerwaniu do bufora pierścieniowego, komendy tekstowe
  zakończone `\n` (`GAIN 1 5`, `TB 7`, `GET 4096`). TX przez DMA.
- Blok binarny: nagłówek (znacznik, N, fs, kanały, wzmocnienia) + surowe kody → `tools/viewer.py`.
- Prędkość: 115200 na start, potem 2 Mbaud (do sprawdzenia, ile przenosi ST-LINK).

## 8. Priorytety przerwań (NVIC, grupa 4, 0 = najwyższy)

| Priorytet | Przerwanie | Robi |
|---|---|---|
| 0 | DMA1 ch1/ch2 (ADC) TC | stop TIM6, flaga |
| 1 | DMA SPI1 TX TC | flaga „pasek wysłany”, CS w górę po ostatnim |
| 2 | TIM7 (1 ms) | przyciski, enkoder Y |
| 3 | LPUART1 RX, DMA TX | bufor pierścieniowy |
| 15 | SysTick | `HAL_GetTick()` |

## 9. Pamięć (128 kB SRAM = SRAM1 80 kB + SRAM2 16 kB + CCM 32 kB)

| Bufor | Rozmiar |
|---|---|
| Próbki: 4 kanały × 4096 × 2 B (jeden blok, bez ping-ponga) | 32 kB |
| FFT 4096 (wejście + wyjście float) + moduł | 32 + 8 kB |
| Dwa paski ekranu 300×10×2 B | 12 kB |
| Kolumny min/max: 4 × 300 × 2 × 2 B | 5 kB |
| Stos / sterta / reszta | 4 + 2 + ~5 kB |
| **Razem** | **~100 kB** |

Ping-pong próbek (+32 kB, akwizycja w trakcie rysowania) tylko jeśli zabraknie klatek/s.
Kalibracja: ostatnia strona flash (2 kB).

## 10. Moduły i pliki

Wszystkie w `Core/Src` i `Core/Inc` (CubeMX nie usuwa obcych plików przy regeneracji).
`main.c` woła tylko `app_init()` i pętlę w sekcjach `USER CODE`.

| Plik | Zależy od | Odpowiada za |
|---|---|---|
| `app.c` | wszystko | `settings_t`, `app_init`, `app_apply`, tryby RUN/STOP/SINGLE, widok czas/FFT |
| `acq.c` | ADC, DMA, TIM6 | akwizycja, wyzwolenie, podstawa czasu, `frame_t` |
| `afe.c` | SPI1, GPIO | 74HC595, DC/AC, kalibracja kod ↔ wolty |
| `view.c` | acq, dsp | kolumny min/max, interpolacja, skalowanie, kursory |
| `dsp.c` | CMSIS-DSP | FFT, okno Hann, Vpp/Vśr/Vrms/f |
| `lcd.c` | SPI1, DMA | ILI9341: inicjalizacja, okno, wysyłka DMA |
| `gfx.c` | lcd | paski, siatka, tekst (czcionka 8×16), paski statusu |
| `ui.c` | TIM3/4/7, GPIO | enkodery, przyciski, kolejka zdarzeń |
| `link.c` | LPUART1 | komendy, wysyłka bloków |
| `calib.c` | flash | zapis/odczyt kalibracji |
| `prof.h` | DWT | pomiar czasu (`PROF_START/STOP`) |

## 11. Poprawki w obecnym `.ioc` (przed pierwszym kodem)

| # | Jest | Ma być | Dlaczego |
|---|---|---|---|
| 1 | **TIM7**: Prescaler 0, Period 44, TRGO, przerwanie **włączone** | TIM7: Prescaler 155, Period 999, przerwanie ✓ (1 ms) | z przerwaniem co 289 ns CPU stanie |
| 2 | TIM6 wyłączony | TIM6: Prescaler 0, Period 44, TRGO = Update, **bez przerwania** | wyzwalacz ADC |
| 3 | PC2 RUN_STOP = **GPIO_Output** | GPIO_Input | to przycisk |
| 4 | PC4 CS_DISP stan początkowy Low | **High** | wyświetlacz nieaktywny po starcie |
| 5 | Stack 0x400, Heap 0x200 | Stack **0x1000**, Heap **0x800** | `printf`, FFT, zmienne lokalne |
| 6 | brak DMA | na razie OK – DMA SPI1_TX dojdzie w kroku z wyświetlaczem, DMA ADC w kroku ADC | |
