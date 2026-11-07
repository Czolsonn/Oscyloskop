/*
 * link.c - odbiór znaków z LPUART1 w przerwaniu do bufora pierścieniowego, składanie linii w pętli.
 */
#include "link.h"
#include "main.h"

#define RXBUF 256                              /* potęga 2 */
static volatile uint8_t  rx[RXBUF];
static volatile uint16_t head;                 /* pisze przerwanie */
static uint16_t tail;                          /* czyta pętla główna */
static char     line[96];
static int      len;

void link_init(void)
{
    LPUART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    SET_BIT(LPUART1->CR1, USART_CR1_RXNEIE_RXFNEIE);       /* przerwanie od odebranego znaku */
    HAL_NVIC_SetPriority(LPUART1_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(LPUART1_IRQn);
}

void LPUART1_IRQHandler(void)
{
    uint32_t isr = LPUART1->ISR;
    if (isr & USART_ISR_RXNE_RXFNE) {
        uint8_t c = (uint8_t)LPUART1->RDR;              /* odczyt kasuje flagę */
        uint16_t h = head;
        if ((uint16_t)(h + 1u - tail) <= RXBUF) {        /* pełny bufor -> znak tracony */
            rx[h & (RXBUF - 1)] = c;
            head = h + 1u;
        }
    }
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE))
        LPUART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
}

int link_getline(char *buf, int max)
{
    while (tail != head) {
        char c = (char)rx[tail & (RXBUF - 1)];
        tail++;
        if (c == '\r' || c == '\n') {
            if (len == 0) continue;                       /* pusta linia / \r\n */
            int n = (len < max - 1) ? len : max - 1;
            for (int i = 0; i < n; i++) buf[i] = line[i];
            buf[n] = 0;
            len = 0;
            return 1;
        }
        if (c == 8 && len > 0) { len--; continue; }       /* backspace z terminala */
        if (len < (int)sizeof line - 1) line[len++] = c;
    }
    return 0;
}
