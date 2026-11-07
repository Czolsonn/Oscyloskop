/*
 * app.c - krok A3: układ ekranu + 4 sztuczne przebiegi animowane, pomiar klatek/s.
 * W kolejnych krokach sztuczne przebiegi zastąpią dane z ADC (acq.c / view.c).
 */
#include "app.h"
#include "gfx.h"
#include "lcd.h"
#include "main.h"
#include <math.h>
#include <stdio.h>

#define BAR_TOP_H  16
#define BAR_BOT_Y  (PLOT_Y + PLOT_H)      /* 216 */
#define COL_BAR    RGB565(28, 28, 28)
#define PI2        6.2831853f

static const uint16_t ch_color[NCH] = { C_YELLOW, C_CYAN, C_MAGENTA, C_LIME };
static const int16_t  ch_zero[NCH]  = { 50, 100, 135, 170 };   /* położenie 0 V w polu (px) */

static float    phase;
static uint32_t t_next, frames_last, cyc_last;

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void draw_static(void)
{
    lcd_fill(C_BLACK);

    /* pasek statusu */
    lcd_fill_rect(0, 0, LCD_W, BAR_TOP_H, COL_BAR);
    lcd_fill_rect(2, 2, 22, 12, RGB565(46, 125, 50));
    lcd_text(4, 4, "RUN", C_WHITE, RGB565(46, 125, 50), 1);
    lcd_text(32, 4, "T:200us/dz", C_WHITE, COL_BAR, 1);
    lcd_text(104, 4, "Trg:CH1 ^ 1,20V", C_YELLOW, COL_BAR, 1);
    lcd_text(200, 4, "AUTO", C_WHITE, COL_BAR, 1);

    /* prawa kolumna ze znacznikami zera kanałów */
    lcd_fill_rect(PLOT_W, PLOT_Y, LCD_W - PLOT_W, PLOT_H, RGB565(18, 18, 18));
    for (int c = 0; c < NCH; c++) {
        uint16_t y = PLOT_Y + ch_zero[c] - 5;
        lcd_fill_rect(PLOT_W + 3, y, 15, 11, ch_color[c]);
        char s[2] = { (char)('1' + c), 0 };
        lcd_text(PLOT_W + 8, y + 2, s, C_BLACK, ch_color[c], 1);
    }

    /* dolny pasek kanałów */
    lcd_fill_rect(0, BAR_BOT_Y, LCD_W, LCD_H - BAR_BOT_Y, COL_BAR);
    const char *lab[NCH] = { "1: 2V DC", "2: 5V AC", "3: 1V DC", "4: 1V DC" };
    for (int c = 0; c < NCH; c++) {
        lcd_fill_rect(2 + c * 62, BAR_BOT_Y + 3, 58, 11, ch_color[c]);
        lcd_text(5 + c * 62, BAR_BOT_Y + 5, lab[c], C_BLACK, ch_color[c], 1);
    }
}

/* Cztery sztuczne przebiegi (zamiast ADC) */
static void make_traces(float ph)
{
    int16_t y[PLOT_W];

    for (int x = 0; x < PLOT_W; x++)                          /* CH1: sinus */
        y[x] = (int16_t)(ch_zero[0] - 38.0f * sinf(PI2 * x / 150.0f + ph));
    gfx_trace_from_y(&gfx_tr[0], y);

    for (int x = 0; x < PLOT_W; x++) {                        /* CH2: prostokąt */
        int p = (x + (int)(ph * 12.0f)) % 75;
        y[x] = (int16_t)(ch_zero[1] + (p < 37 ? -20 : 20));
    }
    gfx_trace_from_y(&gfx_tr[1], y);

    for (int x = 0; x < PLOT_W; x++)                          /* CH3: sinus z modulacją */
        y[x] = (int16_t)(ch_zero[2] - 15.0f * sinf(PI2 * x / 25.0f + 3.0f * ph)
                                          * (0.6f + 0.4f * sinf(PI2 * x / 300.0f)));
    gfx_trace_from_y(&gfx_tr[2], y);

    for (int x = 0; x < PLOT_W; x++) {                        /* CH4: trójkąt */
        float p = fmodf(x / 100.0f + ph * 0.1f, 1.0f);
        y[x] = (int16_t)(ch_zero[3] - 20.0f * (p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p));
    }
    gfx_trace_from_y(&gfx_tr[3], y);
}

void app_init(void)
{
    dwt_init();
    printf("\r\n=== Rejestrator 4ch, krok A3, SYSCLK = %lu Hz, SPI1 /%lu ===\r\n",
           (unsigned long)HAL_RCC_GetSysClockFreq(),
           (unsigned long)(2u << (((SPI1->CR1 >> 3) & 7))));
    lcd_init();
    gfx_init();
    for (int c = 0; c < NCH; c++) {
        gfx_tr[c].color = ch_color[c];
        gfx_tr[c].on = 1;
    }
    gfx_trig_y = ch_zero[0] - 15;
    draw_static();
    t_next = HAL_GetTick() + 1000;
}

void app_poll(void)
{
    gfx_poll();
    if (gfx_busy()) return;

    /* między ramkami: raz na sekundę statystyka (SPI jest wtedy wolne) */
    if ((int32_t)(HAL_GetTick() - t_next) >= 0) {
        t_next += 1000;
        uint32_t f = gfx_frames() - frames_last;
        uint32_t cyc = gfx_render_cycles() - cyc_last;
        frames_last = gfx_frames();
        cyc_last = gfx_render_cycles();
        uint32_t cpu = cyc / 1560000u;                         /* % z 156 MHz */
        char s[24];
        snprintf(s, sizeof s, "%2lufps CPU%2lu%%", (unsigned long)f, (unsigned long)cpu);
        lcd_text(244, 4, s, C_GRAY, COL_BAR, 1);
        printf("%lu klatek/s, rysowanie paskow: %lu %% CPU\r\n", (unsigned long)f, (unsigned long)cpu);
    }

    make_traces(phase);
    phase += 0.08f;
    if (phase > 1000.0f) phase = 0.0f;
    gfx_start_frame();
}
