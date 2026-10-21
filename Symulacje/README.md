# Symulacje – tor analogowy (wersja 4-kanałowa)

Wartości i obliczenia: `Dokumentacja/wartosci_AFE_4kanaly.md`.

## Pliki

| Plik / folder | Zawartość |
|---|---|
| `Analog Front End.asc` | **schemat główny** jednego kanału. Wzmocnienie: `.param gsel` (0 = G1, 1 = G2, 2 = G5, 3 = G10), sprzężenie: `kAC` (0 = DC, 1 = AC) |
| `Analog Front End_v2_1_AC.asc` | charakterystyka 4 wzmocnienia × DC/AC z pomiarami `.meas` (błąd przy 100 kHz i 1 MHz, f_−3dB) |
| `Fig01…Fig09_*.asc` + `.plt` | symulacje do rysunków w pracy; `.plt` ustawia osie po otwarciu wyniku |
| `Model_test_*.asc` | testy nowych modeli (TMUX1104, AQY212S, ESD2CAN36-Q1) |
| `*.asy`, `*.lib` | symbole i modele: OPA354 (model TI w `sboc018c/`), TMUX1104, AQY212S (własne, behawioralne), ESD2CAN36-Q1 (TI) |
| `Datasheets_models/` | karty katalogowe, z których powstały modele |
| `plot's/AFE_v2/LTspice_figures/` | rysunki do pracy (PNG z okna LTspice) i kopie plików Fig |
| `runs/` | skrypty Pythona: `afe_figs.py` (buduje, liczy i fotografuje Fig01–09), `model_tests.py`, `afe_v2_sims.py`, `lt.py`, `ltshot.ps1` |
| `_archiwum_stary_uklad/` | stary układ 1-kanałowy (przekaźniki G6K, LMH6642, DAC trim, zasilanie): symulacje, wyniki, skrypty. Nie jest potrzebne do nowego układu; ścieżki modeli w tych plikach wskazują na stare położenie, więc przed otwarciem trzeba by je przenieść z powrotem |

Pliki `.raw`, `.log`, `.net`, `.op.raw` to wyniki LTspice: można je usunąć, powstaną ponownie po uruchomieniu symulacji.

Wszystkie pliki `.asc` muszą leżeć w tym folderze, obok plików `.asy` (LTspice szuka symboli w folderze schematu).
