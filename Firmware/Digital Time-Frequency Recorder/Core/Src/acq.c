/*
 * acq.c - akwizycja blokowa 4 kanałów + podstawa czasu (kroki A5-A7).
 *
 *   ARM:  obie pary dual uzbrojone (czekają na TIM6 TRGO) + DMA na n słów, potem start TIM6.
 *         Wszystkie 4 ADC startują od tego samego impulsu -> próbki kanałów są jednoczesne.
 *   DMA TC (przerwanie): zatrzymanie TIM6; blok gotowy, gdy skończą obie pary.
 *   acq_poll(): stop ADC, rozdzielenie słów 32-bit na 4 tablice kanałów.
 *
 * Podstawa czasu: TIM6 (PSC, ARR) + nadpróbkowanie ADC (CFGR2: ROVSE, TROVS - każda konwersja
 * wyzwalana timerem, wynik = średnia z 2^k kolejnych próbek -> filtr antyaliasingowy przy wolnych T/dz).
 */
#include "acq.h"
#include "main.h"
#include "adc.h"
#include "tim.h"

/* takty na próbkę wyjściową = 5,2e6 * T/dz [s]; arr1 * 2^ovs = ticks (tabela z architektura_firmware.md) */
static const tb_t tb_tab[TB_COUNT] = {
    /* label    ticks   arr1 ovs   n      spx (próbek/px) */
    { "1us",       45,    45, 0, 4096, 5.2f / 45.0f  },
    { "2us",       45,    45, 0, 4096, 10.4f / 45.0f },
    { "5us",       45,    45, 0, 4096, 26.0f / 45.0f },
    { "10us",      52,    52, 0, 1200, 1.0f },
    { "20us",     104,    52, 1, 1200, 1.0f },
    { "50us",     260,    65, 2, 1200, 1.0f },
    { "100us",    520,    65, 3, 1200, 1.0f },
    { "200us",   1040,    65, 4, 1200, 1.0f },
    { "500us",   2600,   325, 3, 1200, 1.0f },
    { "1ms",     5200,   325, 4, 1200, 1.0f },
    { "2ms",    10400,   325, 5, 1200, 1.0f },
    { "5ms",    26000,  1625, 4, 1200, 1.0f },
    { "10ms",   52000,  1625, 5, 1200, 1.0f },
    { "20ms",  104000,  1625, 6, 1200, 1.0f },
    { "50ms",  260000,  8125, 5, 1200, 1.0f },
};

static uint32_t raw34[ACQ_NMAX];              /* ADC3 | ADC4 << 16  (CH1, CH2) */
static uint32_t raw12[ACQ_NMAX];              /* ADC1 | ADC2 << 16  (CH3, CH4) */
static uint16_t chbuf[ACQ_NCH][ACQ_NMAX];     /* po rozdzieleniu */

#define DONE12 1u
#define DONE34 2u
static volatile uint8_t done;
static uint8_t  ready, running;
static volatile uint32_t t_half, t_end;
static uint32_t half_cyc;

static int      n_cur = 1200;
static float    spx_cur = 1.0f;
static uint32_t fs_cur, ovs_cur = 1;

static ADC_TypeDef *const adcs[4] = { ADC1, ADC2, ADC3, ADC4 };

const tb_t *acq_tb(int idx) { return &tb_tab[idx]; }

void acq_init(void)
{
    /* kalibracja każdego ADC przy wyłączonym ADC, raz po starcie */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc3, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc4, ADC_SINGLE_ENDED);

    /* ADC podrzędne (ADC2, ADC4): przy przepełnieniu nadpisuj wynik. Z "data preserved" po pierwszym
     * przepełnieniu (np. przy zatrzymaniu bloku) rejestr wyniku zamarza i kanał pokazuje stałą wartość. */
    SET_BIT(ADC2->CFGR, ADC_CFGR_OVRMOD);
    SET_BIT(ADC4->CFGR, ADC_CFGR_OVRMOD);
}

void acq_set_timebase(int idx, int use_ovs)
{
    const tb_t *t = &tb_tab[idx];
    uint32_t psc = 1, arr1, ovs_log2 = 0;

    if (use_ovs && t->ovs_log2) {
        arr1 = t->arr1;
        ovs_log2 = t->ovs_log2;
    } else {
        /* bez nadpróbkowania: cały okres w timerze; TIM6 jest 16-bit -> preskaler, gdy trzeba */
        while ((t->ticks % psc) || (t->ticks / psc > 65536u)) psc++;
        arr1 = t->ticks / psc;
    }

    /* nadpróbkowanie: wszystkie 4 ADC (ADC wyłączone po acq_poll/acq_abort) */
    uint32_t cfgr2 = 0;
    if (ovs_log2)
        cfgr2 = ADC_CFGR2_ROVSE | ADC_CFGR2_TROVS
              | ((ovs_log2 - 1u) << ADC_CFGR2_OVSR_Pos)     /* stosunek 2^ovs_log2 */
              | (ovs_log2 << ADC_CFGR2_OVSS_Pos);           /* przesunięcie -> wynik dalej 12-bit */
    for (int i = 0; i < 4; i++)
        MODIFY_REG(adcs[i]->CFGR2, ADC_CFGR2_ROVSE | ADC_CFGR2_TROVS | ADC_CFGR2_OVSR | ADC_CFGR2_OVSS, cfgr2);

    htim6.Instance->PSC = psc - 1u;
    htim6.Instance->ARR = arr1 - 1u;
    htim6.Instance->EGR = TIM_EGR_UG;          /* załadowanie PSC (ADC nieuzbrojone - impuls TRGO bez skutku) */
    htim6.Instance->SR = 0;

    n_cur = t->n;
    spx_cur = t->spx;
    ovs_cur = 1u << ovs_log2;
    fs_cur = 156000000u / (psc * arr1 * ovs_cur);
}

void acq_arm(void)
{
    ready = 0;
    done = 0;
    running = 1;
    /* HAL czyści flagi tylko w ADC głównym - flagi podrzędnych czyścimy sami (zapis 1 kasuje) */
    ADC2->ISR = ADC_ISR_OVR | ADC_ISR_EOC | ADC_ISR_EOS;
    ADC4->ISR = ADC_ISR_OVR | ADC_ISR_EOC | ADC_ISR_EOS;
    HAL_ADCEx_MultiModeStart_DMA(&hadc3, raw34, (uint32_t)n_cur);  /* ADC3+ADC4 czekają na wyzwolenie */
    HAL_ADCEx_MultiModeStart_DMA(&hadc1, raw12, (uint32_t)n_cur);  /* ADC1+ADC2 czekają na wyzwolenie */
    __HAL_TIM_SET_COUNTER(&htim6, 0);
    __HAL_TIM_ENABLE(&htim6);    /* bez HAL_TIM_Base_Start: HAL nie pozwala go wołać drugi raz bez Stop */
}

void acq_abort(void)
{
    htim6.Instance->CR1 &= ~TIM_CR1_CEN;
    if (running) {
        HAL_ADCEx_MultiModeStop_DMA(&hadc3);
        HAL_ADCEx_MultiModeStop_DMA(&hadc1);
    }
    running = 0;
    ready = 0;
}

/* Połowa bloku pary CH1/CH2 - do pomiaru fs między połową a końcem */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc3) t_half = DWT->CYCCNT;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    /* obie pary dostały ten sam, ostatni impuls - można od razu zatrzymać timer */
    htim6.Instance->CR1 &= ~TIM_CR1_CEN;
    if (hadc == &hadc3) { t_end = DWT->CYCCNT; done |= DONE34; }
    if (hadc == &hadc1) done |= DONE12;
}

void acq_poll(void)
{
    if (!running || done != (DONE12 | DONE34)) return;
    HAL_ADCEx_MultiModeStop_DMA(&hadc3);
    HAL_ADCEx_MultiModeStop_DMA(&hadc1);
    running = 0;
    half_cyc = t_end - t_half;

    for (int i = 0; i < n_cur; i++) {                    /* rozdzielenie słów 32-bit */
        uint32_t a = raw34[i], b = raw12[i];
        chbuf[0][i] = (uint16_t)(a & 0xFFFF);            /* CH1 = ADC3 */
        chbuf[1][i] = (uint16_t)(a >> 16);               /* CH2 = ADC4 */
        chbuf[2][i] = (uint16_t)(b & 0xFFFF);            /* CH3 = ADC1 */
        chbuf[3][i] = (uint16_t)(b >> 16);               /* CH4 = ADC2 */
    }
    ready = 1;
}

int acq_busy(void) { return running; }
int acq_take(void) { if (ready) { ready = 0; return 1; } return 0; }
const uint16_t *acq_ch(int ch) { return chbuf[ch]; }
int acq_n(void) { return n_cur; }
float acq_spx(void) { return spx_cur; }
uint32_t acq_fs_hz(void) { return fs_cur; }
uint32_t acq_ovs(void) { return ovs_cur; }
uint32_t acq_half_cycles(void) { return half_cyc; }

int32_t acq_find_trigger(const uint16_t *d, int32_t from, int32_t to,
                         uint16_t level, uint16_t hyst, int edge)
{
    int armed = 0;
    int32_t lo = (int32_t)level - hyst, hi = (int32_t)level + hyst;
    if (from < 1) from = 1;
    for (int32_t i = from; i < to; i++) {
        if (!edge) {                                     /* narastające */
            if (d[i] < lo) armed = 1;
            else if (armed && d[i] >= level) return i;
        } else {                                         /* opadające */
            if (d[i] > hi) armed = 1;
            else if (armed && d[i] <= level) return i;
        }
    }
    return -1;
}
