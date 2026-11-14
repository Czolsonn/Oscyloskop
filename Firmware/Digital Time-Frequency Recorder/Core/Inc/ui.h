/*
 * ui.h - przyciski i enkodery płytki (krok C1) + przycisk B1 na Nucleo.
 *
 * Każde zdarzenie zamienia się na komendę tekstową - tę samą, którą wysyła pilot z PC
 * (tools/remote.py), więc przyciski i pilot działają identycznie.
 *
 *   CH1..CH4  (PC1, PB10, PB11, PB0)  "CHBTN n"    kanał wł. / wybór aktywnego / wył.
 *   RUN/STOP  (PC2)                   "RUNBTN"     krótko: RUN <-> STOP, długo (>0,7 s): "SINGLE"
 *   AUTO      (PC3)                   "MODE T"     AUTO <-> NORM
 *   enkoder T (TIM4, PA11/PA12)       "TROT +/-"   Trigger: poziom (w menu: wybór pozycji); wciśnięcie (PA10) "TPUSH": menu
 *   enkoder X (TIM3, PB4/PB5)         "XROT +/-"   oś czasu: T/dz albo pozycja pozioma; wciśnięcie (PB9) "XPUSH": przełącza
 *   enkoder Y (PB6/PB7 programowo)    "YROT +/-"   oś napięcia aktywnego kanału: V/dz albo offset; wciśnięcie (PA15) "YPUSH": przełącza
 *   B1 na Nucleo (PC13)               "RUNBTN"
 */
#ifndef UI_H_INCLUDED
#define UI_H_INCLUDED

void ui_init(void);
int  ui_poll(char *cmd, int max);   /* 1 = jest komenda do wykonania (w cmd) */

#endif
