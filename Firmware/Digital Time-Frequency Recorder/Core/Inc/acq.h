/*
 * acq.h - akwizycja blokowa 4 kanałów (kroki A5-A7).
 *
 * Pary dual (regular simultaneous), obie wyzwalane tym samym TIM6 TRGO:
 *   ADC3 (master) + ADC4: CH1 = PB1, CH2 = PB15   -> na płytce obsadzone
 *   ADC1 (master) + ADC2: CH3 = PA0, CH4 = PA1    -> na płytce DNP
 * Każda para: jedno DMA, słowa 32-bit (młodsze 16 bit = master, starsze = slave).
 *
 * Częstotliwość próbkowania wybierana z listy konfiguracji (TIM6 + nadpróbkowanie ADC);
 * podstawę czasu (T/dz) i liczbę próbek na piksel liczy app.c.
 */
#ifndef ACQ_H_INCLUDED
#define ACQ_H_INCLUDED

#include <stdint.h>

#define ACQ_NMAX     4096                     /* maks. próbek na kanał w bloku */
#define ACQ_NCH      4
#define ACQ_NCFG     13                       /* liczba konfiguracji fs */

uint32_t    acq_cfg_fs(int cfg);              /* fs konfiguracji [Sa/s], cfg 0 = najszybsza (3,467 MSa/s) */
void        acq_init(void);
void        acq_configure(int cfg, int use_ovs, int n);   /* tylko gdy !acq_busy() */
void        acq_arm(void);                /* start jednego bloku (wszystkie 4 kanały) */
void        acq_abort(void);              /* przerwanie trwającego bloku */
void        acq_poll(void);               /* wołać w pętli głównej */
int         acq_busy(void);               /* 1 = blok w trakcie zbierania */
int         acq_take(void);               /* 1 (jednorazowo) = nowy blok gotowy */
const uint16_t *acq_ch(int ch);           /* próbki kanału ch = 0..3 (CH1..CH4), acq_n() sztuk */
int         acq_n(void);
uint32_t    acq_fs_hz(void);              /* częstotliwość próbek wyjściowych [Sa/s] */
uint32_t    acq_ovs(void);                /* aktywny współczynnik nadpróbkowania */
uint32_t    acq_half_cycles(void);        /* cykle 156 MHz na n/2 próbek (DWT, HT -> TC) */
/* Napięcie z PC0 (V_BAT przez dzielnik 100k/33k): konwersja wstrzykiwana ADC1, średnia z 8.
 * Tylko między blokami (!acq_busy()); zwraca kod 0..4095 albo -1, gdy ADC zajęty. */
int         acq_read_vbat(void);

/* Wyzwolenie: pierwsze zbocze (edge 0 = narastające, 1 = opadające) przez `level` z histerezą `hyst`
 * w zakresie indeksów [from, to). Zwraca indeks pierwszej próbki za progiem albo -1. */
int32_t acq_find_trigger(const uint16_t *d, int32_t from, int32_t to,
                         uint16_t level, uint16_t hyst, int edge);

#endif
