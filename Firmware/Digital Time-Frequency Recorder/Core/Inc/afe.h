/*
 * afe.h - sterowanie torem analogowym płytki: wzmocnienie (74HC595 -> TMUX1104) i AC/DC (PhotoMOS).
 *
 * 74HC595 (U7) na wspólnym SPI1 z wyświetlaczem, zatrzask RCLK = PC10 (CS_REG), OE na GND, MR na 3,3 V
 * -> po włączeniu zasilania wyjścia są przypadkowe, dlatego afe_init() od razu wysyła G1.
 * Bajt (MSB first): g1 | g2<<2 | g3<<4 | g4<<6, g = A1A0 kanału: 0 = G1, 1 = G2, 2 = G5, 3 = G10.
 * AC/DC: PB13 (CH1), PB14 (CH2), PB12 (CH3), PC6 (CH4); 1 = DC (PhotoMOS zwiera kondensator C12).
 */
#ifndef AFE_H_INCLUDED
#define AFE_H_INCLUDED

#include <stdint.h>

#define AFE_NG     4
#define VREF_MV    3300        /* VREF+ = VDDA Nucleo. Na płytce zmierzyć (CN10 pin 7) i wpisać, potem kalibracja C4 */

extern const float afe_gain_in2adc[AFE_NG];   /* wzmocnienie wejście BNC -> pin ADC (dzielnik x PGA) */
extern const char *const afe_gain_name[AFE_NG];  /* "1", "2", "5", "10" */

void afe_init(void);                /* G1 i DC na wszystkich kanałach, wysyłka do 74HC595 (SPI wolne) */
void afe_set_gain(int ch, int g);   /* zapamiętuje; wysyłka w afe_flush() */
void afe_set_dc(int ch, int dc);    /* od razu na GPIO */
int  afe_gain(int ch);
int  afe_dc(int ch);
int  afe_pending(void);             /* 1 = nowe wzmocnienie czeka na wysłanie */
void afe_flush(void);               /* wysyła bajt do 74HC595 - TYLKO gdy SPI wolne (!gfx_busy()) */

#endif
