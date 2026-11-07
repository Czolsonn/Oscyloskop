/*
 * lcd.h - sterownik ILI9341 (SPI1), wersja blokująca (krok A2).
 * Piny: CS = PC4 (CS_DISP), DC = PC11 (DC_RS), RESET = PD2 (RESET).
 */
#ifndef LCD_H_INCLUDED
#define LCD_H_INCLUDED

#include <stdint.h>

#define LCD_W 320
#define LCD_H 240

/* Orientacja (rejestr MADCTL 0x36). Poziomo: 0x28 albo 0xE8 (obrót o 180°).
 * Jeśli obraz jest lustrzany: 0x68 lub 0xA8. Jeśli czerwony i niebieski są
 * zamienione: przełącz bit BGR (0x08). */
#define LCD_MADCTL 0x28

/* Kolory RGB565 */
#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     RGB565(255, 0, 0)
#define C_GREEN   RGB565(0, 255, 0)
#define C_BLUE    RGB565(0, 0, 255)
#define C_YELLOW  RGB565(245, 217, 10)   /* CH1 */
#define C_CYAN    RGB565(79, 195, 247)   /* CH2 */
#define C_MAGENTA RGB565(240, 98, 146)   /* CH3 */
#define C_LIME    RGB565(124, 219, 94)   /* CH4 */
#define C_ORANGE  RGB565(255, 167, 38)   /* wyzwalanie */
#define C_GRAY    RGB565(90, 90, 90)
#define C_DGRAY   RGB565(44, 44, 44)

void lcd_init(void);
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcd_fill(uint16_t color);
void lcd_pixel(uint16_t x, uint16_t y, uint16_t color);
/* Zapis ciągły (dla DMA): okno + RAMWR, CS zostaje niski aż do lcd_end_write() */
void lcd_begin_write(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_end_write(void);
/* Tekst czcionką 5x7 (komórka 6x8 px), scale = 1, 2, 3... */
void lcd_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);

#endif
