/*
 * app.h - logika aplikacji: inicjalizacja i jeden obieg pętli głównej.
 */
#ifndef APP_H_INCLUDED
#define APP_H_INCLUDED

void app_init(void);
void app_poll(void);   /* wołane w while(1), nigdy nie blokuje */

#endif
