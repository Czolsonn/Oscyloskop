/*
 * link.h - odbiór komend tekstowych z PC przez LPUART1 (wirtualny COM ST-LINK, 209700 baud).
 * Wysyłanie: zwykły printf (__io_putchar w main.c).
 *
 * UWAGA: przerwanie LPUART1 obsługuje link.c (LPUART1_IRQHandler). W CubeMX NIE włączaj
 * "LPUART1 global interrupt" - wygenerowany drugi handler da błąd linkera "multiple definition".
 */
#ifndef LINK_H_INCLUDED
#define LINK_H_INCLUDED

void link_init(void);
/* Zwraca 1, gdy przyszła cała linia (zakończona \r lub \n); kopiuje ją do buf bez znaku końca. */
int  link_getline(char *buf, int max);

#endif
