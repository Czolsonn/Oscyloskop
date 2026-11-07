/*
 * ui.c - przycisk B1 (PC13). CubeMX ustawił PC13 jako analogowy (wolny pin), więc tu -> wejście.
 * Nucleo ma na B1 własny rezystor, więc biegunowość wykrywamy z poziomu spoczynkowego po starcie.
 * Filtr drgań: odczyt co 5 ms, zmiana uznana po 4 jednakowych odczytach (20 ms).
 */
#include "ui.h"
#include "main.h"

static GPIO_PinState idle, stable;
static uint8_t  cnt;
static uint32_t t_next;

void ui_init(void)
{
    GPIO_InitTypeDef g = { 0 };
    __HAL_RCC_GPIOC_CLK_ENABLE();
    g.Pin = GPIO_PIN_13;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &g);
    HAL_Delay(2);
    idle = stable = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13);
}

int ui_poll(void)
{
    if ((int32_t)(HAL_GetTick() - t_next) < 0) return EV_NONE;
    t_next = HAL_GetTick() + 5;

    GPIO_PinState s = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13);
    if (s == stable) { cnt = 0; return EV_NONE; }
    if (++cnt < 4) return EV_NONE;
    cnt = 0;
    stable = s;
    return (s != idle) ? EV_RUNSTOP : EV_NONE;       /* zdarzenie przy wciśnięciu */
}
