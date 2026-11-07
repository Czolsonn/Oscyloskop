/*
 * acq.c - akwizycja blokowa 4 kanałów (krok A5).
 *
 *   ARM:  obie pary dual uzbrojone (czekają na TIM6 TRGO) + DMA na ACQ_N słów, potem start TIM6.
 *         Wszystkie 4 ADC startują od tego samego impulsu -> próbki kanałów są jednoczesne.
 *   DMA TC (przerwanie): zatrzymanie TIM6; blok gotowy, gdy skończą obie pary.
 *   acq_poll(): stop ADC, rozdzielenie słów 32-bit na 4 tablice kanałów.
 */
#include "acq.h"
#include "main.h"
#include "adc.h"
#include "tim.h"

static uint32_t raw34[ACQ_N];                 /* ADC3 | ADC4 << 16  (CH1, CH2) */
static uint32_t raw12[ACQ_N];                 /* ADC1 | ADC2 << 16  (CH3, CH4) */
static uint16_t chbuf[ACQ_NCH][ACQ_N];        /* po rozdzieleniu */

#define DONE12 1u
#define DONE34 2u
static volatile uint8_t done;
static uint8_t  ready, running;
static volatile uint32_t t_half, t_end;
static uint32_t half_cyc;

void acq_init(void)
{
    /* kalibracja każdego ADC przy wyłączonym ADC, raz po starcie */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc3, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc4, ADC_SINGLE_ENDED);
    __HAL_TIM_SET_AUTORELOAD(&htim6, ACQ_TIM_DIV - 1);   /* okres próbkowania z acq.h, nie z CubeMX */

    /* ADC podrzędne (ADC2, ADC4): przy przepełnieniu nadpisuj wynik. Z "data preserved" po pierwszym
     * przepełnieniu (np. przy zatrzymaniu bloku) rejestr wyniku zamarza i kanał pokazuje stałą wartość. */
    SET_BIT(ADC2->CFGR, ADC_CFGR_OVRMOD);
    SET_BIT(ADC4->CFGR, ADC_CFGR_OVRMOD);
}

void acq_arm(void)
{
    ready = 0;
    done = 0;
    running = 1;
    /* HAL czyści flagi tylko w ADC głównym - flagi podrzędnych czyścimy sami (zapis 1 kasuje) */
    ADC2->ISR = ADC_ISR_OVR | ADC_ISR_EOC | ADC_ISR_EOS;
    ADC4->ISR = ADC_ISR_OVR | ADC_ISR_EOC | ADC_ISR_EOS;
    HAL_ADCEx_MultiModeStart_DMA(&hadc3, raw34, ACQ_N);  /* ADC3+ADC4 czekają na wyzwolenie */
    HAL_ADCEx_MultiModeStart_DMA(&hadc1, raw12, ACQ_N);  /* ADC1+ADC2 czekają na wyzwolenie */
    __HAL_TIM_SET_COUNTER(&htim6, 0);
    __HAL_TIM_ENABLE(&htim6);    /* od teraz próbka co 45 taktów (bez HAL_TIM_Base_Start:
                                    HAL nie pozwala go wołać drugi raz bez Stop) */
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

    for (int i = 0; i < ACQ_N; i++) {                    /* rozdzielenie słów 32-bit */
        uint32_t a = raw34[i], b = raw12[i];
        chbuf[0][i] = (uint16_t)(a & 0xFFFF);            /* CH1 = ADC3 */
        chbuf[1][i] = (uint16_t)(a >> 16);               /* CH2 = ADC4 */
        chbuf[2][i] = (uint16_t)(b & 0xFFFF);            /* CH3 = ADC1 */
        chbuf[3][i] = (uint16_t)(b >> 16);               /* CH4 = ADC2 */
    }
    ready = 1;
}

int acq_ready(void) { return ready; }
const uint16_t *acq_ch(int ch) { return chbuf[ch]; }
uint32_t acq_half_cycles(void) { return half_cyc; }

int32_t acq_find_trigger(const uint16_t *d, int32_t from, int32_t to, uint16_t level, uint16_t hyst)
{
    int armed = 0;
    if (from < 1) from = 1;
    for (int32_t i = from; i < to; i++) {
        if (d[i] < level - hyst) armed = 1;              /* uzbrojenie poniżej progu */
        else if (armed && d[i] >= level) return i;       /* przejście w górę */
    }
    return -1;
}
