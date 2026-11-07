/*
 * app.c - krok A5: 4 kanały jednocześnie (ADC3+ADC4 i ADC1+ADC2 w trybie dual, wspólny TIM6).
 * Sygnał testowy: sinus 10 kHz z DAC1 (PA4) - kabelek przykładany kolejno do wejść kanałów.
 *
 * Ekran: każdy kanał we własnym pasie 50 px (pełny zakres ADC 0..3,3 V na 2 działki).
 * Wyzwolenie automatyczne: na kanale z największym Vpp, poziom = środek jego zakresu.
 * Pętla: blok 4 x 4096 próbek (1,18 ms) -> wyzwolenie -> kolumny min/max -> ramka DMA -> nowy blok.
 */
#include "app.h"
#include "acq.h"
#include "gfx.h"
#include "lcd.h"
#include "main.h"
#include "dac.h"
#include "tim.h"
#include <math.h>
#include <stdio.h>

#define BAR_TOP_H  16
#define BAR_BOT_Y  (PLOT_Y + PLOT_H)      /* 216 */
#define COL_BAR    RGB565(28, 28, 28)

#define DEC        4                      /* próbek na piksel -> 1200 próbek = 346 us na ekranie */
#define BAND_H     (PLOT_H / NCH)         /* 50 px na kanał */
#define VREF_MV    3300                   /* Nucleo: VREF+ = VDDA = 3,3 V (na płytce 3,25 V) */
#define MIN_VPP    200                    /* [kody] poniżej tego kanał uznajemy za "bez sygnału" */

/* DAC: 50 próbek na okres, aktualizacja 500 kHz (TIM2) -> sinus 10 kHz */
#define DAC_PTS    50
static uint16_t dac_tab[DAC_PTS * 10];    /* 10 okresów - rzadsze przerwania DMA */

static const uint16_t ch_color[NCH] = { C_YELLOW, C_CYAN, C_MAGENTA, C_LIME };
static const char *ch_pin[NCH] = { "PB1", "PB15", "PA0", "PA1" };

static uint32_t t_next, frames_last;
static uint32_t half_min = 0xFFFFFFFFu;   /* najkrótszy czas połowy bloku w ostatniej sekundzie */
static uint16_t vmin[NCH], vmax[NCH];     /* zakres każdego kanału w bloku [kody] */
static int      src = -1;                 /* kanał wyzwalający, -1 = brak sygnału */
static uint32_t f_mHz, triggered;

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void dac_start(void)
{
    for (int i = 0; i < DAC_PTS * 10; i++)               /* 2048 +- 1200 kodów = 0,68...2,62 V */
        dac_tab[i] = (uint16_t)(2048.0f + 1200.0f * sinf(6.2831853f * (float)i / DAC_PTS));
    HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)dac_tab, DAC_PTS * 10, DAC_ALIGN_12B_R);
    /* DMA DAC chodzi w kółko - przerwania połowy/końca nie są potrzebne */
    __HAL_DMA_DISABLE_IT(hdac1.DMA_Handle1, DMA_IT_HT | DMA_IT_TC);
    HAL_TIM_Base_Start(&htim2);
}

/* Kod ADC -> piksel w pasie kanału ch (pełny zakres 0..4095 = 50 px) */
static inline int16_t code_to_y(int ch, int32_t code)
{
    return (int16_t)(ch * BAND_H + (BAND_H - 1) - code * BAND_H / 4096);
}

static void draw_static(void)
{
    lcd_fill(C_BLACK);
    lcd_fill_rect(0, 0, LCD_W, BAR_TOP_H, COL_BAR);
    lcd_fill_rect(PLOT_W, PLOT_Y, LCD_W - PLOT_W, PLOT_H, RGB565(18, 18, 18));
    lcd_fill_rect(0, BAR_BOT_Y, LCD_W, LCD_H - BAR_BOT_Y, COL_BAR);

    for (int c = 0; c < NCH; c++) {                      /* znacznik kanału na środku jego pasa */
        uint16_t y = PLOT_Y + c * BAND_H + BAND_H / 2 - 5;
        lcd_fill_rect(PLOT_W + 3, y, 15, 11, ch_color[c]);
        char s[2] = { (char)('1' + c), 0 };
        lcd_text(PLOT_W + 8, y + 2, s, C_BLACK, ch_color[c], 1);
    }
}

/* Dolny pasek: Vpp każdego kanału (odświeżany raz na sekundę) */
static void draw_bottom(void)
{
    for (int c = 0; c < NCH; c++) {
        uint32_t mv = (uint32_t)(vmax[c] - vmin[c]) * VREF_MV / 4095;
        char s[16];
        snprintf(s, sizeof s, "%d:%s %lu.%02luV", c + 1, ch_pin[c],
                 (unsigned long)(mv / 1000), (unsigned long)(mv / 10 % 100));
        lcd_fill_rect(2 + c * 79, BAR_BOT_Y + 3, 76, 11, ch_color[c]);
        lcd_text(4 + c * 79, BAR_BOT_Y + 5, s, C_BLACK, ch_color[c], 1);
    }
}

/* Częstotliwość: średni odstęp między przejściami w górę w całym bloku */
static uint32_t measure_f_mHz(const uint16_t *d, uint16_t level, uint16_t hyst)
{
    int32_t first = acq_find_trigger(d, 1, ACQ_N, level, hyst), last = first, n = 0;
    while (last >= 0) {
        int32_t nx = acq_find_trigger(d, last + 1, ACQ_N, level, hyst);
        if (nx < 0) break;
        last = nx;
        n++;
    }
    return (n > 0) ? (uint32_t)((uint64_t)ACQ_FS_HZ * 1000u * (uint32_t)n / (uint32_t)(last - first)) : 0;
}

/* Blok -> kolumny min/max wszystkich kanałów.
 * Wyzwolenie z ułamkiem próbki na kanale src; wszystkie kanały rysowane od tej samej chwili
 * (próbki są jednoczesne, więc przesunięcia między kanałami na ekranie są prawdziwe). */
static void build_view(void)
{
    /* 1. zakres każdego kanału, wybór kanału wyzwalającego (największe Vpp) */
    uint32_t best = MIN_VPP;
    src = -1;
    for (int c = 0; c < NCH; c++) {
        const uint16_t *d = acq_ch(c);
        uint16_t mn = 4095, mx = 0;
        for (int i = 0; i < ACQ_N; i++) {
            if (d[i] < mn) mn = d[i];
            if (d[i] > mx) mx = d[i];
        }
        vmin[c] = mn;
        vmax[c] = mx;
        if ((uint32_t)(mx - mn) > best) { best = mx - mn; src = c; }
    }

    /* 2. wyzwolenie: poziom w połowie zakresu, histereza 5 % Vpp */
    const int32_t half = PLOT_W * DEC / 2;               /* 600 próbek */
    float pos0 = 0.0f;                                   /* bez wyzwolenia: od początku bloku */
    triggered = 0;
    f_mHz = 0;
    gfx_trig_y = -1;
    if (src >= 0) {
        const uint16_t *d = acq_ch(src);
        uint16_t level = (uint16_t)((vmin[src] + vmax[src]) / 2);
        uint16_t hyst = (uint16_t)((vmax[src] - vmin[src]) / 20);
        int32_t t = acq_find_trigger(d, half, ACQ_N - half, level, hyst);
        if (t >= 0) {
            float a = d[t - 1], b = d[t];
            float frac = (b > a) ? ((float)level - a) / (b - a) : 1.0f;
            pos0 = (float)(t - 1) + frac - (float)half;
            triggered = 1;
        }
        gfx_trig_y = code_to_y(src, level);
        f_mHz = measure_f_mHz(d, level, hyst);
    }

    /* 3. kolumny min/max z interpolacją liniową od chwili wyzwolenia */
    for (int c = 0; c < NCH; c++) {
        const uint16_t *d = acq_ch(c);
        trace_t *tr = &gfx_tr[c];
        float prev = 0.0f;
        for (int x = 0; x < PLOT_W; x++) {
            float mn = 1e9f, mx = -1e9f, v = 0.0f;
            for (int k = 0; k < DEC; k++) {
                float pos = pos0 + (float)(x * DEC + k);
                int32_t i = (int32_t)pos;
                float fr = pos - (float)i;
                v = (float)d[i] + fr * ((float)d[i + 1] - (float)d[i]);
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
            if (x > 0) {                                 /* ciągłość z poprzednią kolumną */
                if (prev < mn) mn = prev;
                if (prev > mx) mx = prev;
            }
            prev = v;
            tr->ymin[x] = code_to_y(c, (int32_t)(mx + 0.5f));   /* większy kod = wyżej */
            tr->ymax[x] = code_to_y(c, (int32_t)(mn + 0.5f));
        }
    }
}

void app_init(void)
{
    dwt_init();
    printf("\r\n=== Rejestrator 4ch, krok A5 (4 kanaly, ADC3+ADC4 / ADC1+ADC2 dual), SYSCLK = %lu Hz ===\r\n",
           (unsigned long)HAL_RCC_GetSysClockFreq());
    lcd_init();
    gfx_init();
    for (int c = 0; c < NCH; c++) {
        gfx_tr[c].color = ch_color[c];
        gfx_tr[c].on = 1;
    }
    draw_static();

    dac_start();
    acq_init();
    acq_arm();
    t_next = HAL_GetTick() + 1000;
}

void app_poll(void)
{
    gfx_poll();
    acq_poll();
    if (gfx_busy() || !acq_ready()) return;

    if (acq_half_cycles() < half_min) half_min = acq_half_cycles();
    build_view();
    acq_arm();              /* następny blok zbiera się w czasie wysyłania ramki */

    /* SPI wolne: raz na sekundę paski statusu i linia w terminalu */
    if ((int32_t)(HAL_GetTick() - t_next) >= 0) {
        t_next += 1000;
        uint32_t fps = gfx_frames() - frames_last;
        frames_last = gfx_frames();
        uint32_t fs = (half_min != 0xFFFFFFFFu)
                      ? (uint32_t)((uint64_t)(ACQ_N / 2) * 156000000u / half_min) : 0;
        char s[64];
        if (src >= 0)
            snprintf(s, sizeof s, "%s CH%d f=%lu.%02lukHz fs=%lu.%03luMSa/s %2lufps   ",
                     triggered ? "TRIG" : "AUTO", src + 1,
                     (unsigned long)(f_mHz / 1000000u), (unsigned long)(f_mHz / 10000u % 100u),
                     (unsigned long)(fs / 1000000u), (unsigned long)(fs / 1000u % 1000u),
                     (unsigned long)fps);
        else
            snprintf(s, sizeof s, "brak sygnalu   fs=%lu.%03luMSa/s %2lufps         ",
                     (unsigned long)(fs / 1000000u), (unsigned long)(fs / 1000u % 1000u),
                     (unsigned long)fps);
        lcd_text(4, 4, s, triggered ? C_WHITE : C_ORANGE, COL_BAR, 1);
        draw_bottom();
        printf("%s | Vpp[mV] CH1=%lu CH2=%lu CH3=%lu CH4=%lu | %lu cykli/2048 pr.\r\n", s,
               (unsigned long)((vmax[0] - vmin[0]) * VREF_MV / 4095),
               (unsigned long)((vmax[1] - vmin[1]) * VREF_MV / 4095),
               (unsigned long)((vmax[2] - vmin[2]) * VREF_MV / 4095),
               (unsigned long)((vmax[3] - vmin[3]) * VREF_MV / 4095),
               (unsigned long)half_min);
        half_min = 0xFFFFFFFFu;
    }

    gfx_start_frame();
}
