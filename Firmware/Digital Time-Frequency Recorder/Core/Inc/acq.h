/*
 * acq.h - akwizycja blokowa 4 kanałów (krok A5).
 *
 * Pary dual (regular simultaneous), obie wyzwalane tym samym TIM6 TRGO:
 *   ADC3 (master) + ADC4: CH1 = PB1, CH2 = PB15   -> na płytce obsadzone
 *   ADC1 (master) + ADC2: CH3 = PA0, CH4 = PA1    -> na płytce DNP
 * Każda para: jedno DMA, słowa 32-bit (młodsze 16 bit = master, starsze = slave).
 */
#ifndef ACQ_H_INCLUDED
#define ACQ_H_INCLUDED

#include <stdint.h>

#define ACQ_N        4096                     /* próbek na kanał w bloku */
#define ACQ_NCH      4
/* ARR + 1 timera TIM6: 45 = 3,467 MSa/s. Sprawdzone 07.11: przy 45 i przy 48 taktach czas 2048 próbek
 * przekracza ideał o te same ~50 cykli (narzut HAL w przerwaniach) -> przy 45 wyzwolenia nie giną. */
#define ACQ_TIM_DIV  45u
#define ACQ_FS_HZ    (156000000u / ACQ_TIM_DIV)

void            acq_init(void);
void            acq_arm(void);                /* start jednego bloku (wszystkie 4 kanały) */
void            acq_poll(void);               /* wołać w pętli głównej */
int             acq_ready(void);              /* 1 = blok gotowy */
const uint16_t *acq_ch(int ch);               /* próbki kanału ch = 0..3 (CH1..CH4), ACQ_N sztuk */
uint32_t        acq_half_cycles(void);        /* cykle 156 MHz na ACQ_N/2 próbek (DWT, HT -> TC) */

/* Wyzwolenie: pierwsze zbocze narastające przez `level` (z histerezą `hyst`)
 * w zakresie indeksów [from, to). Zwraca indeks albo -1. */
int32_t acq_find_trigger(const uint16_t *d, int32_t from, int32_t to, uint16_t level, uint16_t hyst);

#endif
