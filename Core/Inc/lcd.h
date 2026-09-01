/**
 * lcd.h -- framebuffer primitives for the RK043FN48H panel.
 *
 * The panel is wired with 24 data lines, so the LTDC *interface* is RGB888
 * (set in CubeMX, pins configured by HAL_LTDC_MspInit). The *framebuffer*
 * format is a separate choice: LTDC_PIXEL_FORMAT_RGB565, 2 bytes per pixel,
 * which the LTDC expands to 8/8/8 on the pins itself. RGB() below packs a
 * colour into that 16-bit word -- it says nothing about the pin interface.
 *
 * The framebuffer lives in AXI SRAM (512 KB at 0x24000000, otherwise unused).
 * 480*272*2 = 261120 bytes, so the external SDRAM is not needed at all.
 * LTDC itself is initialised in main.c (MX_LTDC_Init).
 */
#ifndef LCD_H_INCLUDED
#define LCD_H_INCLUDED

#include <stdint.h>

#define LCD_W  480
#define LCD_H  272
#define LCD_FB ((volatile uint16_t *) 0x24000000)

#define RGB(r, g, b) \
  ((uint16_t) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* One palette for the whole application. */
#define COL_BG     RGB(  0,   0,  16)
#define COL_PANEL  RGB( 14,  20,  34)
#define COL_LINE   RGB( 70,  86, 104)
#define COL_TEXT   RGB(228, 236, 240)
#define COL_DIM    RGB(112, 128, 140)
#define COL_ACCENT RGB(  0, 255, 120)
#define COL_WARN   RGB(255, 190,   0)
#define COL_X      RGB( 96, 196, 255)
#define COL_O      RGB(255, 140,  90)

void LCD_Fill(uint16_t c);
void LCD_FillRect(int x, int y, int w, int h, uint16_t c);
void LCD_Frame(int x, int y, int w, int h, int t, uint16_t c);

/* 5x7 glyphs scaled by an integer factor; advance is 6*scale per character.
 * Lower case folds to upper case, unknown characters draw blank. */
void LCD_Text(int x, int y, const char *s, uint16_t fg, int scale);
int  LCD_TextW(const char *s, int scale);
void LCD_TextCenter(int y, const char *s, uint16_t fg, int scale);
void LCD_TextCenterIn(int x, int w, int y, const char *s, uint16_t fg, int scale);

#endif /* LCD_H */
