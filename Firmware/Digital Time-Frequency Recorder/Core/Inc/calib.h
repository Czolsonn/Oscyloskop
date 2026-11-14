/*
 * calib.h - kalibracja zera (krok C4, część 1): przesunięcie kodu ADC dla każdego kanału
 * i każdego wzmocnienia, zapisywane w ostatniej stronie flash (przetrwa reset i wyłączenie).
 */
#ifndef CALIB_H_INCLUDED
#define CALIB_H_INCLUDED

#include <stdint.h>

extern int16_t cal_zero[4][4];          /* [kanał][wzmocnienie G1/G2/G5/G10] w kodach ADC */

void calib_load(void);                  /* przy starcie; brak danych -> zera */
int  calib_save(void);                  /* 0 = OK */

#endif
