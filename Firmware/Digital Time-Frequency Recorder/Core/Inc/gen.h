/*
 * gen.h - generator sygnału testowego na DAC1 (PA4 = gniazdo A2), sterowany komendą GEN.
 * Tylko do testów na samej Nucleo; na płytce PA4 nie jest używany.
 */
#ifndef GEN_H_INCLUDED
#define GEN_H_INCLUDED

#include <stdint.h>

enum { GEN_OFF = 0, GEN_SIN, GEN_SQR, GEN_TRI };

/* Ustawia kształt i częstotliwość [Hz] (1 Hz ... 100 kHz). Zwraca faktyczną częstotliwość [mHz]. */
uint32_t gen_set(int shape, uint32_t f_hz);
int      gen_shape(void);
uint32_t gen_f_mhz(void);
const char *gen_name(int shape);

#endif
