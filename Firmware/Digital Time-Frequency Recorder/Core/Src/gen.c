/*
 * gen.c - generator sygnału testowego: DAC1 kanał 1 (PA4), aktualizacja z TIM2 TRGO, DMA w kółko.
 *
 * Jeden okres w tablicy. Do 10 kHz: 100 punktów na okres; powyżej: DAC na 1 MSa/s (maks. dla
 * bufora wyjściowego) i mniej punktów (min. 10 -> 100 kHz). Amplituda 2048 +- 1200 kodów = 0,68...2,62 V.
 */
#include "gen.h"
#include "main.h"
#include "dac.h"
#include "tim.h"
#include <math.h>

#define GEN_PTS_MAX 1000
static uint16_t tab[GEN_PTS_MAX];
static int      shape_cur = GEN_OFF;
static uint32_t f_mhz_cur;

const char *gen_name(int s)
{
    static const char *n[] = { "OFF", "SIN", "SQR", "TRI" };
    return (s >= 0 && s <= 3) ? n[s] : "?";
}
int gen_shape(void) { return shape_cur; }
uint32_t gen_f_mhz(void) { return f_mhz_cur; }

uint32_t gen_set(int shape, uint32_t f_hz)
{
    HAL_TIM_Base_Stop(&htim2);
    HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
    shape_cur = shape;
    f_mhz_cur = 0;
    if (shape == GEN_OFF || f_hz == 0) {               /* wyłączony = stałe 1,65 V (środek zakresu) */
        shape = shape_cur = GEN_OFF;
        f_hz = 1000;
    }
    if (f_hz > 100000u) f_hz = 100000u;

    uint32_t pts = (f_hz <= 10000u) ? 100u : 1000000u / f_hz;          /* >= 10 */
    uint32_t ticks = (156000000u + f_hz * pts / 2u) / (f_hz * pts);      /* takty TIM2 na punkt */
    if (ticks < 156u) ticks = 156u;                                       /* maks. 1 MSa/s */

    for (uint32_t i = 0; i < pts; i++) {
        float ph = (float)i / (float)pts;                                 /* 0..1 */
        float v;
        if (shape == GEN_OFF)      v = 0.0f;
        else if (shape == GEN_SIN) v = sinf(6.2831853f * ph);
        else if (shape == GEN_SQR) v = (ph < 0.5f) ? 1.0f : -1.0f;
        else                       v = (ph < 0.5f) ? (4.0f * ph - 1.0f) : (3.0f - 4.0f * ph);
        tab[i] = (uint16_t)(2048.0f + 1200.0f * v);
    }

    __HAL_TIM_SET_AUTORELOAD(&htim2, ticks - 1u);
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)tab, pts, DAC_ALIGN_12B_R);
    __HAL_DMA_DISABLE_IT(hdac1.DMA_Handle1, DMA_IT_HT | DMA_IT_TC);     /* DMA w kółko, bez przerwań */
    HAL_TIM_Base_Start(&htim2);

    f_mhz_cur = (shape == GEN_OFF) ? 0 : (uint32_t)(156000000000ull / ((uint64_t)ticks * pts));
    return f_mhz_cur;
}
