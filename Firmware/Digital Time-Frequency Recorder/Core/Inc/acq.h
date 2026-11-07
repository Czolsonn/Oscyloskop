/*
 * acq.h - akwizycja blokowa 4 kanałów + podstawa czasu (kroki A5-A7).
 *
 * Pary dual (regular simultaneous), obie wyzwalane tym samym TIM6 TRGO:
 *   ADC3 (master) + ADC4: CH1 = PB1, CH2 = PB15   -> na płytce obsadzone
 *   ADC1 (master) + ADC2: CH3 = PA0, CH4 = PA1    -> na płytce DNP
 * Każda para: jedno DMA, słowa 32-bit (młodsze 16 bit = master, starsze = slave).
 */
#ifndef ACQ_H_INCLUDED
#define ACQ_H_INCLUDED

#include <stdint.h>

#define ACQ_NMAX     4096                     /* maks. próbek na kanał w bloku */
#define ACQ_NCH      4
#define ACQ_PX_DIV   30                       /* pikseli na działkę w poziomie */

/* Podstawa czasu. Liczba taktów 156 MHz na próbkę wyjściową = 156e6 * T/dz / 30 px.
 * Od 10 us/dz: dokładnie 1 próbka na piksel (timer + ewentualnie nadpróbkowanie ADC).
 * 1-5 us/dz: ADC na maksimum (45 taktów = 3,467 MSa/s), mniej niż 1 próbka na piksel -> interpolacja. */
typedef struct {
    const char *label;      /* "20us" */
    uint32_t    ticks;      /* takty 156 MHz na próbkę wyjściową (dla 1-5 us: 45) */
    uint16_t    arr1;       /* ARR+1 timera, gdy nadpróbkowanie włączone */
    uint8_t     ovs_log2;   /* nadpróbkowanie 2^ovs_log2 (0 = brak) */
    uint16_t    n;          /* próbek na kanał w bloku */
    float       spx;        /* próbek na piksel */
} tb_t;

#define TB_COUNT 15

const tb_t *acq_tb(int idx);
void        acq_init(void);
void        acq_set_timebase(int idx, int use_ovs);   /* tylko gdy !acq_busy() */
void        acq_arm(void);                /* start jednego bloku (wszystkie 4 kanały) */
void        acq_abort(void);              /* przerwanie trwającego bloku */
void        acq_poll(void);               /* wołać w pętli głównej */
int         acq_busy(void);               /* 1 = blok w trakcie zbierania */
int         acq_take(void);               /* 1 (jednorazowo) = nowy blok gotowy */
const uint16_t *acq_ch(int ch);           /* próbki kanału ch = 0..3 (CH1..CH4), acq_n() sztuk */
int         acq_n(void);
float       acq_spx(void);
uint32_t    acq_fs_hz(void);              /* częstotliwość próbek wyjściowych [Sa/s] */
uint32_t    acq_ovs(void);                /* aktywny współczynnik nadpróbkowania */
uint32_t    acq_half_cycles(void);        /* cykle 156 MHz na n/2 próbek (DWT, HT -> TC) */

/* Wyzwolenie: pierwsze zbocze (edge 0 = narastające, 1 = opadające) przez `level` z histerezą `hyst`
 * w zakresie indeksów [from, to). Zwraca indeks pierwszej próbki za progiem albo -1. */
int32_t acq_find_trigger(const uint16_t *d, int32_t from, int32_t to,
                         uint16_t level, uint16_t hyst, int edge);

#endif
