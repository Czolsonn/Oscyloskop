# Tor analogowy – wartości końcowe (wersja 4-kanałowa)

Stan na 2026-10-19. Wartości dla jednego kanału (CH1; kanały CH2–CH4 są identyczne).
Oznaczenia: **KiCad** = oznaczenie na schemacie CH1, **LTspice** = oznaczenie w `Symulacje/Analog Front End.asc`.
„obl.” = obliczenie ręczne, „sym.” = wynik LTspice, „DS” = karta katalogowa.

## 1. Elementy

| Funkcja | KiCad | LTspice | Wartość | Uwagi |
|---|---|---|---|---|
| Ochrona wejścia (TVS) | D1 | U7 | ESD2CAN36-Q1 | dwukierunkowa, V_RWM = 36 V, V_BR = 40 V, 2,8 pF (DS) |
| Rezystor szeregowy dzielnika | R20 | R4 | 953 kΩ, 1 %, 0805 | |
| Kondensator kompensacyjny górny | C6 | C1 | 3,3 pF C0G, ±0,25 pF | |
| Rezystor dzielnika do VREF+ | R27 | R1 | **90,9 kΩ**, 1 % | do VREF+ = 3,25 V (Nucleo, CN10 pin 7) |
| Rezystor dzielnika do masy | R29 | R5 | **100 kΩ**, 1 % | |
| Kondensator kompensacyjny dolny | C13 | C3 | 47 pF C0G | |
| Trymer | C14 | C5 | 5,5–30 pF | nastawa ok. **15,6 pF** (sym.), na płytce ustawić prostokątem |
| Pojemność montażowa węzła N (założona) | – | C6 | 3 pF | założenie |
| Kondensator sprzęgający AC | C12 | C2 | 10 nF C0G, 50 V | |
| Rezystor polaryzacji (tryb AC) | R30 | R10 | 10 MΩ | do VMID = 1,625 V |
| Przełącznik AC/DC | K2 | U1 | AQY212S (PhotoMOS) | zwarty = DC; R_on 0,83 Ω, C_out ≈ 70 pF (DS) |
| Rezystor diody LED PhotoMOS | R28 | R7 | 390 Ω | z GPIO 3,3 V: I_F = 5,5 mA |
| Wzmacniacz PGA | U5 | U4 | OPA354, zasilanie +5 V | |
| Rezystor sprzężenia zwrotnego R_f | R15 | R6 | 499 Ω | |
| Kondensator C_f | C7 | C4 | 22 pF C0G | biegun 14,5 MHz |
| Multiplekser wzmocnienia | U3 | U6 | TMUX1104 | VDD = EN = +5 V, A0/A1 z 74HC595 |
| R_g dla G = 2 (S2) | R12 | R11 | 499 Ω | |
| R_g dla G = 5 (S3) | R13 | R9 | 124 Ω | |
| R_g dla G = 10 (S4) | R14 | R8 | 54,9 Ω | |
| Filtr – rezystory | R17, R18 | R12, R13 | 1,96 kΩ | |
| Filtr – kondensator w pętli | C11 | C7 | 30 pF C0G | |
| Filtr – kondensator do masy | C15 | C8 | 22 pF C0G | |
| Wzmacniacz filtru / bufor ADC | U9 | U2 | OPA354, zasilanie +3,3 V | |
| Rezystor szeregowy ADC | R16 | R14 | 47 Ω | |
| Kondensator przy wejściu ADC | C10 | C9 | 150 pF C0G | |
| Bufor VMID (wspólny dla 4 kanałów) | U10 | U3 | OPA354, +5 V | dzielnik R19/R21 100 k/100 k z VREF+, 10 µF + 100 nF; na wyjściu 1 Ω + 10 µF, 100 nF |

## 2. Obliczenia

### 2.1 Dzielnik wejściowy

Rezystancja zastępcza dolnej części dzielnika (twierdzenie Thevenina):

R_d = R27 ∥ R29 = 90,9 kΩ ∥ 100 kΩ = 47,62 kΩ

W trybie DC do węzła N dochodzi jeszcze R30 = 10 MΩ (przez zwarty PhotoMOS), więc R_d' = 47,62 kΩ ∥ 10 MΩ = 47,39 kΩ.

| Wielkość | Wzór | Wartość | Sym. |
|---|---|---|---|
| Napięcie Thevenina V_th | 3,25 · R29/(R27 + R29) | 1,7025 V | – |
| Wzmocnienie dzielnika A | R_d'/(R20 + R_d') | 0,04737 (1 : 21,1) | 0,04737 |
| Napięcie węzła N przy U_we = 0 V | z bilansu prądów w węźle N | 1,6215 V (−3,5 mV od VMID) | – |
| Rezystancja wejściowa | R20 + R_d | 1,0006 MΩ | – |
| Pojemność wejściowa | C6 szeregowo z C_low, + TVS | ≈ 3,1 pF + 2,8 pF + złącze ≈ 6–10 pF | – |

### 2.2 Kompensacja dzielnika

Warunek kompensacji: R20 · C6 = R_d' · C_low

| Wielkość | Wartość |
|---|---|
| Stała czasowa τ = R20 · C6 | 953 kΩ · 3,3 pF = 3,145 µs |
| Wymagane C_low = τ / R_d' | 66,4 pF |
| Trymer = C_low − C13 − montaż − wejście OPA | 66,4 − 47 − 3 − ~0,8 ≈ 15,6 pF (sym.: 15,6 pF) |
| Zakres trymera 5,5–30 pF pokrywa C6 | 2,8 … 4,0 pF (tolerancja 3,3 pF ± 0,25 pF mieści się) |

### 2.3 Sprzężenie AC

f_d = 1 / (2π · R30 · C12) = 1 / (2π · 10 MΩ · 10 nF) = **1,59 Hz** (sym.: 1,575 Hz)

### 2.4 Wzmacniacz PGA

G = 1 + R_f / (R_g + R_on), R_on = 2 Ω (TMUX1104, DS)

| Pozycja | A1 A0 | R_g | G obl. | G sym. |
|---|---|---|---|---|
| G1 | 00 (S1 wolny) | ∞ | 1,000 | 1,000 |
| G2 | 01 (S2) | 499 Ω | 1,996 | 1,996 |
| G5 | 10 (S3) | 124 Ω | 4,960 | 4,957 |
| G10 | 11 (S4) | 54,9 Ω | 9,770 | 9,775 |

### 2.5 Filtr antyaliasingowy (Sallen-Key, Bessel 2. rzędu, G = 1)

| Wielkość | Wzór | Wartość |
|---|---|---|
| Częstotliwość własna f_0 | 1 / (2π · R · √(C11 · C15)) | 3,16 MHz |
| Dobroć Q | ½ · √(C11 / C15) | 0,584 (Bessel: 0,577) |
| f_−3dB | f_0 / 1,272 | 2,49 MHz |

### 2.6 Wejście ADC i odniesienie

| Wielkość | Wartość |
|---|---|
| VREF+ (pełna skala ADC) | 3,25 V |
| VMID | 1,625 V |
| 1 LSB (12 bit) | 3,25 V / 4096 = 0,793 mV |
| Filtr R16/C10: f = 1/(2π · 47 Ω · 150 pF) | 22,6 MHz |
| Prąd LED PhotoMOS: (3,3 − 1,14 V) / 390 Ω | 5,5 mA (I_F,on max 3 mA, DS) |

## 3. Zakresy pomiarowe

Wzmocnienie całkowite wejście → ADC: A · G. Pełna skala: ±1,625 V / (A · G).
Zakres liniowy: wyjście OPA354 co najmniej 0,1 V od szyn zasilania (DS: 0,1 V typ.), czyli ±1,525 V.

| Wzmocnienie | Wejście → ADC | Pełna skala ADC | Zakres liniowy | Rozdzielczość (1 LSB na wejściu) |
|---|---|---|---|---|
| G1 | 0,04737 | ±34,3 V | ±32,2 V | 16,7 mV |
| G2 | 0,09455 | ±17,2 V | ±16,1 V | 8,4 mV |
| G5 | 0,2348 | ±6,92 V | ±6,49 V | 3,4 mV |
| G10 | 0,4630 | ±3,51 V | ±3,29 V | 1,7 mV |

Napięcie zera przy G10 w trybie DC: 1,625 V − 3,5 mV · 9,77 ≈ 1,591 V (sym. 1,601 V). Kasuje to kalibracja zera w programie.

## 4. Wyniki symulacji (LTspice)

Modele: OPA354 (TI), TMUX1104 i AQY212S (modele behawioralne z kart katalogowych), ESD2CAN36-Q1 (TI).
Metoda całkowania Gear (domyślna metoda trapezów dawała w przebiegach 1 MHz fałszywe oscylacje numeryczne).

### 4.1 Charakterystyka częstotliwościowa (rys. 1)

| Wzmocnienie | Wejście → ADC (1 kHz) | Błąd 1 % przy | Błąd 3 % przy | Błąd przy 1 MHz | f_−3dB górna | f_−3dB dolna (AC) |
|---|---|---|---|---|---|---|
| G1 | 0,04737 | 501 kHz | 832 kHz | −4,5 % | 2,57 MHz | 1,58 Hz |
| G2 | 0,09456 | 479 kHz | 794 kHz | −5,3 % | 2,41 MHz | 1,58 Hz |
| G5 | 0,2350 | 437 kHz | 724 kHz | −5,9 % | 2,34 MHz | 1,58 Hz |
| G10 | 0,4628 | 417 kHz | 692 kHz | −6,9 % | 2,24 MHz | 1,58 Hz |

### 4.2 Przebiegi czasowe (rys. 2–9)

| Rys. | Sygnał wejściowy | Ustawienie | Pin ADC (min … max) | Wniosek |
|---|---|---|---|---|
| 2 | sinus 1 kHz, 24 V amplitudy | G1, DC | 0,486 … 2,762 V | wzmocnienie 0,0474, środek 1,624 V |
| 3 | prostokąt 10 kHz, ±24 V | G1, DC | 0,485 … 2,763 V | płaskie szczyty, czyli dzielnik skompensowany |
| 4 | sinus 1 kHz, 2 V amplitudy | G10, DC | 0,666 … 2,520 V | wzmocnienie 0,4635, zero 1,601 V |
| 5 | 24 V DC + 1 V sinus 10 kHz | G1 DC / G10 AC | 2,715 … 2,809 V / 1,185 … 2,111 V | tryb AC usuwa 24 V i pozwala użyć G10 (tętnienia 94 mV → 926 mV) |
| 6 | sinus 1 kHz, 40 V (poza zakresem) | G1, DC | 0,017 … 3,283 V | czyste obcięcie, bez odwrócenia fazy |
| 7 | sinus 1 MHz, 32 V | G1, DC | 0,173 … 3,075 V | amplituda −4,3 % (filtr) |
| 8 | prostokąt 1 MHz, ±32 V | G1, DC | 0,084 … 3,161 V | zbocza zaokrąglone przez filtr, przerzut 20 mV |
| 9 | prostokąt 100 kHz, ±32 V (zbocze) | G1, DC | – | czas opadania 10–90 % = 135 ns |

### 4.3 Harmoniczne prostokąta 1 MHz i aliasing

Tłumienie filtru (G1, względem 1 kHz) i miejsce, w które harmoniczna odbija się po próbkowaniu:

| Harmoniczna | Tłumienie | Alias przy 3,47 Msps (4 kanały) | Alias przy 5,47 Msps (2 kanały) |
|---|---|---|---|
| 3 MHz | −4,1 dB | 0,47 MHz | 2,47 MHz |
| 5 MHz | −9,3 dB | 1,53 MHz | 0,47 MHz |
| 7 MHz | −13,8 dB | 0,06 MHz | 1,53 MHz |
| 9 MHz | −17,5 dB | 1,41 MHz | 1,94 MHz |
| 11 MHz | −20,6 dB | 0,59 MHz | 0,06 MHz |

## 5. Parametry przyrządu (do rozdziału z wynikami)

| Parametr | Wartość |
|---|---|
| Liczba kanałów | 4 |
| Impedancja wejściowa | 1 MΩ ∥ ok. 6–10 pF |
| Zakresy (pełna skala) | ±34,3 V / ±17,2 V / ±6,9 V / ±3,5 V (G1 / G2 / G5 / G10) |
| Zakres liniowy | ±32 V / ±16 V / ±6,5 V / ±3,3 V |
| Rozdzielczość (12 bit) | 16,7 / 8,4 / 3,4 / 1,7 mV |
| Sprzężenie | DC lub AC (f_d = 1,6 Hz) |
| Pasmo analogowe −3 dB | 2,2–2,6 MHz |
| Sinus, błąd amplitudy ≤ 1 % / ≤ 3 % | do 420 kHz / do 690 kHz (najgorszy przypadek G10) |
| Sinus, maksymalna częstotliwość | 1 MHz (błąd 4,5–7 %, korekcja w programie) |
| Prostokąt, wierny kształt (≥ 10 próbek na okres) | do ok. 350 kHz (4 kanały, 3,47 Msps) / 550 kHz (2 kanały, 5,47 Msps) |
| Czas narastania toru 10–90 % | 135 ns |
| Ochrona wejścia | TVS 36 V (przebicie 40 V), dalej prąd ograniczony przez 953 kΩ |

## 6. Rysunki do pracy

Pliki: `Symulacje/plot's/AFE_v2/LTspice_figures/Fig01…Fig09.png` (zrzuty okna wykresów LTspice).
Ponowne przeliczenie: otworzyć `Symulacje/Fig0x_….asc` (musi leżeć obok plików `.asy`), uruchomić, LTspice sam wczyta ustawienia osi z pliku `.plt`.
Na wszystkich przebiegach czasowych górny panel to napięcie na wejściu BNC V(in), dolny to napięcie na pinie ADC V(v_out).

Propozycje podpisów:

1. Charakterystyka amplitudowa toru od wejścia do pinu ADC dla wzmocnień G = 1, 2, 5, 10 (od dołu do góry); krzywe opadające poniżej 10 Hz – sprzężenie AC, poziome – sprzężenie DC.
2. Sinus 1 kHz o amplitudzie 24 V, wzmocnienie G = 1, sprzężenie DC.
3. Prostokąt 10 kHz ±24 V, G = 1 – płaskie szczyty potwierdzają kompensację dzielnika.
4. Sinus 1 kHz o amplitudzie 2 V, G = 10.
5. Napięcie 24 V z tętnieniami 1 V (10 kHz): sprzężenie DC przy G = 1 (przebieg u góry dolnego panelu) i sprzężenie AC przy G = 10 (przebieg wyśrodkowany).
6. Sygnał poza zakresem pomiarowym (sinus 40 V) – obcięcie na granicach zakresu ADC.
7. Sinus 1 MHz o amplitudzie 32 V – górna granica pasma.
8. Prostokąt 1 MHz ±32 V – ograniczenie pasma filtrem antyaliasingowym.
9. Zbocze prostokąta ±32 V po przejściu przez tor – czas opadania 135 ns.
