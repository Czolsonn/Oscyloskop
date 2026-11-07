/*
 * gfx.c - renderer paskowy pola przebiegów + DMA SPI1.
 *
 * Ramka = 20 pasków po 300x10 px (6000 B). Dwa bufory paska:
 *   DMA wysyła pasek k z bufora k&1, w tym czasie CPU rysuje pasek k+1 do drugiego bufora.
 * Piksele w buforze mają zamienione bajty (SPI 8-bit wysyła najpierw młodszy bajt z pamięci,
 * a ILI9341 chce najpierw starszy) - dzięki temu SPI zostaje 8-bitowe (wspólne z 74HC595).
 */
#include "gfx.h"
#include "lcd.h"
#include "main.h"
#include "spi.h"
#include <string.h>

trace_t gfx_tr[NCH];
int16_t gfx_trig_y = -1;

static uint16_t strip[2][PLOT_W * STRIP_H];
static uint16_t row_tpl[4][PLOT_W];     /* szablony wierszy siatki */
enum { R_BG, R_DOTS, R_HGRID, R_SOLID };

static volatile uint8_t dma_busy;
static uint8_t  active;                 /* ramka w toku */
static int8_t   ready = -1;             /* numer paska narysowanego i czekającego na DMA */
static uint8_t  next_render;
static uint32_t frames, render_cyc;

#define SW(c)      ((uint16_t)(((c) >> 8) | ((c) << 8)))
#define COL_BG     SW(C_BLACK)
#define COL_GRID   SW(RGB565(60, 60, 60))
#define COL_AXIS   SW(RGB565(110, 110, 110))

void gfx_init(void)
{
    for (int x = 0; x < PLOT_W; x++) {
        int vline = (x % DIV_X == 0) || (x == PLOT_W - 1);
        int vaxis = (x == PLOT_W / 2) || (x == 0) || (x == PLOT_W - 1);
        /* zwykły wiersz: tło, ramka i oś środkowa pionowa */
        row_tpl[R_BG][x]    = vaxis ? COL_AXIS : COL_BG;
        /* wiersz z kropkami pionowych linii siatki */
        row_tpl[R_DOTS][x]  = vaxis ? COL_AXIS : (vline ? COL_GRID : COL_BG);
        /* pozioma linia siatki (kropkowana) */
        row_tpl[R_HGRID][x] = vaxis ? COL_AXIS : ((x % 3 == 0) ? COL_GRID : COL_BG);
        /* ramka górna/dolna i oś środkowa pozioma */
        row_tpl[R_SOLID][x] = COL_AXIS;
    }
    for (int c = 0; c < NCH; c++) gfx_tr[c].on = 0;
}

static int row_type(int y)
{
    if (y == 0 || y == PLOT_H - 1 || y == PLOT_H / 2) return R_SOLID;
    if (y % DIV_Y == 0) return R_HGRID;
    if (y % 5 == 0) return R_DOTS;
    return R_BG;
}

static void render_strip(int s, uint16_t *buf)
{
    uint32_t t0 = DWT->CYCCNT;
    int y0 = s * STRIP_H, y1 = y0 + STRIP_H - 1;

    /* 1. tło + siatka */
    for (int r = 0; r < STRIP_H; r++) {
        int y = y0 + r;
        memcpy(&buf[r * PLOT_W], row_tpl[row_type(y)], sizeof row_tpl[0]);
        /* podziałka na osi środkowej poziomej (co 6 px, +-2 px) */
        if (y >= PLOT_H / 2 - 2 && y <= PLOT_H / 2 + 2)
            for (int x = 0; x < PLOT_W; x += DIV_X / 5) buf[r * PLOT_W + x] = COL_AXIS;
    }
    /* podziałka na osi środkowej pionowej (co 5 px) */
    for (int r = 0; r < STRIP_H; r++)
        if ((y0 + r) % 5 == 0)
            for (int x = PLOT_W / 2 - 2; x <= PLOT_W / 2 + 2; x++) buf[r * PLOT_W + x] = COL_AXIS;

    /* 2. linia poziomu wyzwolenia (przerywana) */
    if (gfx_trig_y >= y0 && gfx_trig_y <= y1) {
        uint16_t c = SW(C_ORANGE);
        uint16_t *row = &buf[(gfx_trig_y - y0) * PLOT_W];
        for (int x = 0; x < PLOT_W; x++) if ((x & 7) < 4) row[x] = c;
    }

    /* 3. przebiegi: pionowe odcinki [ymin, ymax] przycięte do paska */
    for (int ch = 0; ch < NCH; ch++) {
        const trace_t *t = &gfx_tr[ch];
        if (!t->on) continue;
        uint16_t c = SW(t->color);
        for (int x = 0; x < PLOT_W; x++) {
            int a = t->ymin[x], b = t->ymax[x];
            if (b < y0 || a > y1) continue;
            if (a < y0) a = y0;
            if (b > y1) b = y1;
            uint16_t *p = &buf[(a - y0) * PLOT_W + x];
            for (int y = a; y <= b; y++, p += PLOT_W) *p = c;
        }
    }
    render_cyc += DWT->CYCCNT - t0;
}

int gfx_busy(void) { return active; }
uint32_t gfx_frames(void) { return frames; }
uint32_t gfx_render_cycles(void) { return render_cyc; }

void gfx_start_frame(void)
{
    if (active) return;
    active = 1;
    lcd_begin_write(PLOT_X, PLOT_Y, PLOT_X + PLOT_W - 1, PLOT_Y + PLOT_H - 1);
    render_strip(0, strip[0]);
    ready = 0;
    next_render = 1;
    gfx_poll();
}

void gfx_poll(void)
{
    if (!active || dma_busy) return;

    if (ready >= 0) {
        dma_busy = 1;
        if (hspi1.hdmatx == NULL ||       /* brak DMA SPI1_TX w CubeMX -> wyślij blokująco */
            HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)strip[ready & 1], sizeof strip[0]) != HAL_OK) {
            dma_busy = 0;
            HAL_SPI_Transmit(&hspi1, (uint8_t *)strip[ready & 1], sizeof strip[0], HAL_MAX_DELAY);
        }
        ready = -1;
        if (next_render < NSTRIPS) {      /* rysuj następny pasek, gdy DMA wysyła bieżący */
            render_strip(next_render, strip[next_render & 1]);
            ready = next_render++;
        }
        return;
    }
    /* nic do wysłania i DMA wolne -> koniec ramki */
    lcd_end_write();
    active = 0;
    frames++;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi1) dma_busy = 0;
}

void gfx_trace_from_y(trace_t *t, const int16_t *y)
{
    for (int x = 0; x < PLOT_W; x++) {
        int a = y[x], b = y[x];
        if (x > 0) {                      /* połącz z poprzednią kolumną, żeby linia była ciągła */
            int p = y[x - 1];
            if (p < a) a = (a + p) / 2; else if (p > b) b = (b + p) / 2;
        }
        if (x < PLOT_W - 1) {
            int n = y[x + 1];
            if (n < a) a = (a + n + 1) / 2; else if (n > b) b = (b + n) / 2;
        }
        if (a < 0) a = 0;
        if (b > PLOT_H - 1) b = PLOT_H - 1;
        t->ymin[x] = (int16_t)a;
        t->ymax[x] = (int16_t)b;
    }
}
