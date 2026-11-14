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

/* Konfiguracje fs: takty 156 MHz na próbkę wyjściową = arr1 * 2^ovs_log2 (TIM6 + nadpróbkowanie ADC).
 * Wartości z architektura_firmware.md (tabela podstaw czasu, 30 px/dz). */
typedef struct { uint32_t ticks; uint16_t arr1; uint8_t ovs_log2; } cfg_t;
static const cfg_t cfg_tab[ACQ_NCFG] = {
    {     45,   45, 0 },   /* 3,467 MSa/s */
    {     52,   52, 0 },   /* 3 MSa/s     */
    {    104,   52, 1 },   /* 1,5 MSa/s   */
    {    260,   65, 2 },   /* 600 kSa/s   */
    {    520,   65, 3 },   /* 300 kSa/s   */
    {   1040,   65, 4 },   /* 150 kSa/s   */
    {   2600,  325, 3 },   /* 60 kSa/s    */
    {   5200,  325, 4 },   /* 30 kSa/s    */
    {  10400,  325, 5 },   /* 15 kSa/s    */
    {  26000, 1625, 4 },   /* 6 kSa/s     */
    {  52000, 1625, 5 },   /* 3 kSa/s     */
    { 104000, 1625, 6 },   /* 1,5 kSa/s   */
    { 260000, 8125, 5 },   /* 600 Sa/s    */
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
static uint32_t fs_cur, ovs_cur = 1;

static ADC_TypeDef *const adcs[4] = { ADC1, ADC2, ADC3, ADC4 };

uint32_t acq_cfg_fs(int cfg) { return 156000000u / cfg_tab[cfg].ticks; }

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

    /* V_BAT na PC0 = ADC1_IN6 jako kanał WSTRZYKIWANY ADC1 (regularne kanały zajmuje akwizycja).
     * Źródło ma ~25 kOhm (100k || 33k) -> długi czas próbkowania 247,5 cykla (4,8 us). */
    ADC_InjectionConfTypeDef j = { 0 };
    j.InjectedChannel = ADC_CHANNEL_6;
    j.InjectedRank = ADC_INJECTED_RANK_1;
    j.InjectedSamplingTime = ADC_SAMPLETIME_247CYCLES_5;
    j.InjectedSingleDiff = ADC_SINGLE_ENDED;
    j.InjectedOffsetNumber = ADC_OFFSET_NONE;
    j.InjectedNbrOfConversion = 1;
    j.InjectedDiscontinuousConvMode = DISABLE;
    j.AutoInjectedConv = DISABLE;
    j.QueueInjectedContext = DISABLE;
    j.ExternalTrigInjecConv = ADC_INJECTED_SOFTWARE_START;
    j.ExternalTrigInjecConvEdge = ADC_EXTERNALTRIGINJECCONV_EDGE_NONE;
    j.InjecOversamplingMode = DISABLE;
    HAL_ADCEx_InjectedConfigChannel(&hadc1, &j);
}

int acq_read_vbat(void)
{
    if (running) return -1;
    uint32_t sum = 0;
    for (int k = 0; k < 8; k++) {
        if (HAL_ADCEx_InjectedStart(&hadc1) != HAL_OK) return -1;
        if (HAL_ADCEx_InjectedPollForConversion(&hadc1, 2) != HAL_OK) { HAL_ADCEx_InjectedStop(&hadc1); return -1; }
        sum += HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
    }
    HAL_ADCEx_InjectedStop(&hadc1);
    return (int)((sum + 4u) / 8u);
}

void acq_configure(int cfg, int use_ovs, int n)
{
    const cfg_t *t = &cfg_tab[cfg];
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

    n_cur = (n < 64) ? 64 : (n > ACQ_NMAX) ? ACQ_NMAX : n;
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
