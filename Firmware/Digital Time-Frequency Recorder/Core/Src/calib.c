/*
 * calib.c - zapis/odczyt kalibracji w ostatniej stronie flash STM32G474RE (512 kB).
 * Tryb dwóch banków (DBANK = 1, domyślny): strony 2 kB, ostatnia = bank 2 strona 127 @ 0x0807F800.
 * Tryb jednego banku: strony 4 kB, ostatnia = strona 127 @ 0x0807F000.
 * Program zajmuje ~70 kB od 0x08000000, więc koniec flasha jest wolny.
 */
#include "calib.h"
#include "main.h"
#include <string.h>

#define CAL_MAGIC 0x43414C31u            /* "CAL1" */

typedef struct {
    uint32_t magic;
    int16_t  zero[4][4];
    uint32_t sum;                        /* prosta suma kontrolna */
    uint32_t pad[2];                     /* rozmiar = 48 B, wielokrotność 8 B (zapis po 64 bity) */
} cal_rec_t;

int16_t cal_zero[4][4];

static uint32_t page_addr(void)
{
    return READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK) ? 0x0807F800u : 0x0807F000u;
}

static uint32_t checksum(const cal_rec_t *r)
{
    const uint8_t *p = (const uint8_t *)r->zero;
    uint32_t s = 0x1234u;
    for (unsigned i = 0; i < sizeof r->zero; i++) s = s * 31u + p[i];
    return s;
}

void calib_load(void)
{
    const cal_rec_t *r = (const cal_rec_t *)page_addr();
    if (r->magic == CAL_MAGIC && r->sum == checksum(r)) memcpy(cal_zero, r->zero, sizeof cal_zero);
    else memset(cal_zero, 0, sizeof cal_zero);
}

int calib_save(void)
{
    cal_rec_t r;
    memset(&r, 0, sizeof r);
    r.magic = CAL_MAGIC;
    memcpy(r.zero, cal_zero, sizeof r.zero);
    r.sum = checksum(&r);

    FLASH_EraseInitTypeDef e = { 0 };
    uint32_t err = 0, addr = page_addr();
    e.TypeErase = FLASH_TYPEERASE_PAGES;
    e.NbPages = 1;
    if (READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK)) { e.Banks = FLASH_BANK_2; e.Page = 127; }
    else                                         { e.Banks = FLASH_BANK_1; e.Page = 127; }

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    int rc = (HAL_FLASHEx_Erase(&e, &err) == HAL_OK) ? 0 : 1;
    for (unsigned i = 0; rc == 0 && i < sizeof r / 8; i++) {
        uint64_t dw;
        memcpy(&dw, (const uint8_t *)&r + 8u * i, 8);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + 8u * i, dw) != HAL_OK) rc = 2;
    }
    HAL_FLASH_Lock();
    return rc;
}
