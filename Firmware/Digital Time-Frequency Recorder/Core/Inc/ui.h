/*
 * ui.h - wejścia użytkownika. Na razie: przycisk B1 USER na Nucleo (PC13) = RUN/STOP.
 * Przyciski i enkodery płytki dojdą w kroku C1.
 */
#ifndef UI_H_INCLUDED
#define UI_H_INCLUDED

enum { EV_NONE = 0, EV_RUNSTOP };

void ui_init(void);
int  ui_poll(void);     /* zwraca zdarzenie albo EV_NONE */

#endif
