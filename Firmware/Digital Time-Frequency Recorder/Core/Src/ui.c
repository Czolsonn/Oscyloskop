/*
 * ui.c - przyciski i enkodery płytki (krok C1) + B1 na Nucleo.
 *
 * Przyciski: na płytce zewnętrzne 10k do 3,3 V, wciśnięcie = 0. Dodatkowo włączamy wewnętrzne
 * podciąganie (~40k), żeby na samej Nucleo (bez płytki) wejścia nie wisiały w powietrzu - wtedy
 * przycisk można zasymulować zwierając pin kabelkiem do GND. Na płytce nie przeszkadza.
 * Filtr drgań: odczyt co 5 ms, zmiana uznana po 4 jednakowych odczytach (20 ms).
 *
 * Enkodery T i X liczy sprzęt (TIM4, TIM3 w trybie enkodera), Y - przerwanie TIM7 co 1 ms
 * (tablica przejść kwadraturowych). ENC_DIV = liczba zliczeń na jeden "ząbek" enkodera EC11.
 */
#include "ui.h"
#include "main.h"
#include "tim.h"
#include <stdio.h>
#include <string.h>

#define ENC_DIV   4          /* EC11: 4 zliczenia na ząbek (jeśli jeden ząbek = 2 kroki, wpisz 2) */
#define ENC_T_DIR 1          /* odwrócenie kierunku: -1 */
#define ENC_X_DIR 1
#define ENC_Y_DIR 1
#define LONG_MS   700

typedef struct { GPIO_TypeDef *port; uint16_t pin; } pin_t;
enum { B_CH1, B_CH2, B_CH3, B_CH4, B_RUN, B_AUTO, B_TPUSH, B_XPUSH, B_YPUSH, B_B1, NBTN };
static const pin_t btn[NBTN] = {
    { CH1_EN_GPIO_Port, CH1_EN_Pin }, { CH2_EN_GPIO_Port, CH2_EN_Pin },
    { CH3_EN_GPIO_Port, CH3_EN_Pin }, { CH4_EN_GPIO_Port, CH4_EN_Pin },
    { RUN_STOP_GPIO_Port, RUN_STOP_Pin }, { AUTO_GPIO_Port, AUTO_Pin },
    { T_PUSH_GPIO_Port, T_PUSH_Pin }, { X_PUSH_GPIO_Port, X_PUSH_Pin }, { Y_PUSH_GPIO_Port, Y_PUSH_Pin },
    { GPIOC, GPIO_PIN_13 },                                   /* B1 na Nucleo */
};
/* wejścia enkoderów - tylko podciąganie */
static const pin_t enc_pins[] = {
    { T_A_GPIO_Port, T_A_Pin }, { T_B_GPIO_Port, T_B_Pin }, { X_A_GPIO_Port, X_A_Pin },
    { X_B_GPIO_Port, X_B_Pin }, { Y_A_GPIO_Port, Y_A_Pin }, { Y_B_GPIO_Port, Y_B_Pin },
};

static uint8_t  stable[NBTN], cnt[NBTN], idle_b1, long_fired;
static uint32_t t_scan, t_run_down;
static uint16_t t_last, x_last;
static int32_t  t_acc, x_acc, y_used;
static volatile int32_t y_cnt;
static uint8_t  y_prev;

/* kolejka komend */
#define QN 8
static char q[QN][16];
static uint8_t qh, qt;
static void push(const char *s)
{
    if ((uint8_t)(qh - qt) >= QN) return;
    snprintf(q[qh % QN], sizeof q[0], "%s", s);
    qh++;
}

static void pull_up(GPIO_TypeDef *port, uint16_t pin)
{
    uint32_t n = (uint32_t)__builtin_ctz(pin);
    MODIFY_REG(port->PUPDR, 3u << (2u * n), 1u << (2u * n));
}

static uint8_t read_pressed(int i)
{
    uint8_t lvl = (HAL_GPIO_ReadPin(btn[i].port, btn[i].pin) == GPIO_PIN_SET);
    if (i == B_B1) return lvl != idle_b1;     /* B1: biegunowość z poziomu po starcie */
    return !lvl;                              /* płytka: wciśnięty = 0 */
}

void ui_init(void)
{
    /* B1 (PC13): CubeMX ustawił go jako analogowy - przestawiamy na wejście */
    GPIO_InitTypeDef g = { 0 };
    g.Pin = GPIO_PIN_13;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;                     /* Nucleo ma własny rezystor na B1 */
    HAL_GPIO_Init(GPIOC, &g);

    for (int i = 0; i < NBTN - 1; i++) pull_up(btn[i].port, btn[i].pin);
    for (unsigned i = 0; i < sizeof enc_pins / sizeof enc_pins[0]; i++) pull_up(enc_pins[i].port, enc_pins[i].pin);
    HAL_Delay(2);
    idle_b1 = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET);
    for (int i = 0; i < NBTN; i++) stable[i] = read_pressed(i);

    HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL);          /* enkoder T */
    HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);          /* enkoder X */
    t_last = (uint16_t)__HAL_TIM_GET_COUNTER(&htim4);
    x_last = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);
    y_prev = (uint8_t)((HAL_GPIO_ReadPin(Y_A_GPIO_Port, Y_A_Pin) << 1) | HAL_GPIO_ReadPin(Y_B_GPIO_Port, Y_B_Pin));
    HAL_TIM_Base_Start_IT(&htim7);                           /* 1 ms: enkoder Y */
}

/* TIM7 co 1 ms: dekodowanie kwadraturowe enkodera Y */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    static const int8_t qdec[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
    if (htim != &htim7) return;
    uint8_t s = (uint8_t)((((Y_A_GPIO_Port->IDR & Y_A_Pin) != 0) << 1) | ((Y_B_GPIO_Port->IDR & Y_B_Pin) != 0));
    y_cnt += qdec[(y_prev << 2) | s];
    y_prev = s;
}

static void on_press(int i)
{
    char s[16];
    switch (i) {
    case B_CH1: case B_CH2: case B_CH3: case B_CH4:
        snprintf(s, sizeof s, "CHBTN %d", i - B_CH1 + 1); push(s); break;
    case B_AUTO:  push("MODE T"); break;
    case B_TPUSH: push("TPUSH"); break;
    case B_XPUSH: push("XPUSH"); break;
    case B_YPUSH: push("YPUSH"); break;
    case B_B1:    push("RUNBTN"); break;
    case B_RUN:   t_run_down = HAL_GetTick(); long_fired = 0; break;   /* decyzja przy puszczeniu */
    default: break;
    }
}

static void encoders(void)
{
    uint16_t t = (uint16_t)__HAL_TIM_GET_COUNTER(&htim4), x = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);
    t_acc += (int16_t)(t - t_last) * ENC_T_DIR;  t_last = t;
    x_acc += (int16_t)(x - x_last) * ENC_X_DIR;  x_last = x;
    int32_t y = y_cnt * ENC_Y_DIR;

    while (t_acc >= ENC_DIV)  { t_acc -= ENC_DIV; push("TROT +"); }
    while (t_acc <= -ENC_DIV) { t_acc += ENC_DIV; push("TROT -"); }
    while (x_acc >= ENC_DIV)  { x_acc -= ENC_DIV; push("XROT +"); }
    while (x_acc <= -ENC_DIV) { x_acc += ENC_DIV; push("XROT -"); }
    while (y - y_used >= ENC_DIV)  { y_used += ENC_DIV; push("YROT +"); }
    while (y - y_used <= -ENC_DIV) { y_used -= ENC_DIV; push("YROT -"); }
}

int ui_poll(char *cmd, int max)
{
    if ((int32_t)(HAL_GetTick() - t_scan) >= 0) {
        t_scan = HAL_GetTick() + 5;
        for (int i = 0; i < NBTN; i++) {
            uint8_t p = read_pressed(i);
            if (p == stable[i]) { cnt[i] = 0; continue; }
            if (++cnt[i] < 4) continue;
            cnt[i] = 0;
            stable[i] = p;
            if (p) on_press(i);
            else if (i == B_RUN && !long_fired) push("RUNBTN");          /* krótkie wciśnięcie */
        }
        if (stable[B_RUN] && !long_fired && HAL_GetTick() - t_run_down >= LONG_MS) {
            long_fired = 1;
            push("SINGLE");                                            /* długie wciśnięcie */
        }
        encoders();
    }
    if (qt == qh) return 0;
    snprintf(cmd, (size_t)max, "%s", q[qt % QN]);
    qt++;
    return 1;
}
