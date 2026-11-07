/*
 * gfx.h - pole przebiegów 300x200 px rysowane paskami 300x10 px przez DMA (krok A3).
 * CPU rysuje pasek B, podczas gdy DMA wysyła pasek A. Bez bufora całej ramki.
 */
#ifndef GFX_H_INCLUDED
#define GFX_H_INCLUDED

#include <stdint.h>

#define PLOT_X   0      /* położenie pola na ekranie */
#define PLOT_Y   16
#define PLOT_W   300    /* 10 dz x 30 px */
#define PLOT_H   200    /* 8 dz x 25 px  */
#define DIV_X    30
#define DIV_Y    25
#define STRIP_H  10
#define NSTRIPS  (PLOT_H / STRIP_H)
#define NCH      4

/* Przebieg jako kolumny: dla każdego x zakres pionowy [ymin, ymax] (0 = góra pola) */
typedef struct {
    int16_t  ymin[PLOT_W];
    int16_t  ymax[PLOT_W];
    uint16_t color;     /* RGB565 */
    uint8_t  on;
} trace_t;

extern trace_t gfx_tr[NCH];
extern int16_t gfx_trig_y;          /* poziom wyzwolenia w pikselach, -1 = brak linii */

void     gfx_init(void);
int      gfx_busy(void);            /* 1 = trwa wysyłanie ramki, nie ruszać SPI */
void     gfx_start_frame(void);     /* zaczyna ramkę (tylko gdy !gfx_busy()) */
void     gfx_poll(void);            /* wołać w pętli głównej jak najczęściej */
uint32_t gfx_frames(void);          /* licznik wysłanych ramek */
uint32_t gfx_render_cycles(void);   /* suma cykli CPU spędzonych na rysowaniu pasków */

/* Pomocnicza: z tablicy y[x] robi kolumny min/max połączone z poprzednią kolumną */
void gfx_trace_from_y(trace_t *t, const int16_t *y);

#endif
