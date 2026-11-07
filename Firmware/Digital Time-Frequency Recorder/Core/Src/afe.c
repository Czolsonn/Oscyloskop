/*
 * afe.c - wzmocnienie (74HC595 -> TMUX1104) i AC/DC (PhotoMOS) czterech kanałów.
 *
 * SPI1 pracuje dla wyświetlacza z 39 MHz; 74HC595 przy 3,3 V tyle nie wytrzyma (fmax ~ 15-20 MHz),
 * więc na czas jednego bajtu do rejestru SPI zwalnia do 156 MHz / 16 = 9,75 MHz.
 * Przez rejestr przesuwnika i tak przechodzą wszystkie dane dla wyświetlacza, ale wyjścia zmieniają się
 * tylko przy zboczu narastającym RCLK (PC10), które dajemy wyłącznie po naszym bajcie.
 */
#include "afe.h"
#include "main.h"
#include "spi.h"

/* z Dokumentacja/wartosci_AFE_4kanaly.md, tab. "Zakresy pomiarowe" */
const float afe_gain_in2adc[AFE_NG] = { 0.04737f, 0.09455f, 0.2348f, 0.4630f };
const char *const afe_gain_name[AFE_NG] = { "1", "2", "5", "10" };

static uint8_t gain[4], dc[4], pending;

static GPIO_TypeDef *const dc_port[4] = { DCAC_CH1_GPIO_Port, DCAC_CH2_GPIO_Port, DCAC_CH3_GPIO_Port, DCAC_CH4_GPIO_Port };
static const uint16_t      dc_pin[4]  = { DCAC_CH1_Pin, DCAC_CH2_Pin, DCAC_CH3_Pin, DCAC_CH4_Pin };

void afe_set_gain(int ch, int g)
{
    if (g < 0) g = 0;
    if (g >= AFE_NG) g = AFE_NG - 1;
    if (gain[ch] != g) { gain[ch] = (uint8_t)g; pending = 1; }
}

void afe_set_dc(int ch, int d)
{
    dc[ch] = (uint8_t)(d != 0);
    HAL_GPIO_WritePin(dc_port[ch], dc_pin[ch], dc[ch] ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

int afe_gain(int ch) { return gain[ch]; }
int afe_dc(int ch) { return dc[ch]; }
int afe_pending(void) { return pending; }

void afe_flush(void)
{
    uint8_t b = (uint8_t)(gain[0] | gain[1] << 2 | gain[2] << 4 | gain[3] << 6);

    while (SPI1->SR & SPI_SR_BSY) { }
    uint32_t cr1 = SPI1->CR1;
    CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);                            /* BR wolno zmieniać tylko przy SPE = 0 */
    MODIFY_REG(SPI1->CR1, SPI_CR1_BR, SPI_BAUDRATEPRESCALER_16);
    SET_BIT(SPI1->CR1, SPI_CR1_SPE);
    HAL_SPI_Transmit(&hspi1, &b, 1, 10);
    while (SPI1->SR & SPI_SR_BSY) { }
    CLEAR_BIT(SPI1->CR1, SPI_CR1_SPE);
    SPI1->CR1 = cr1 & ~SPI_CR1_SPE;                               /* z powrotem /4 dla wyświetlacza */
    SET_BIT(SPI1->CR1, SPI_CR1_SPE);

    /* zatrzask: zbocze narastające RCLK przepisuje rejestr przesuwnika na wyjścia QA..QH */
    HAL_GPIO_WritePin(CS_REG_GPIO_Port, CS_REG_Pin, GPIO_PIN_RESET);
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();   /* > 20 ns */
    HAL_GPIO_WritePin(CS_REG_GPIO_Port, CS_REG_Pin, GPIO_PIN_SET);
    pending = 0;
}

void afe_init(void)
{
    for (int c = 0; c < 4; c++) {
        gain[c] = 0;            /* G1 - najszerszy zakres, bezpieczny na start */
        afe_set_dc(c, 1);       /* DC */
    }
    afe_flush();
}
