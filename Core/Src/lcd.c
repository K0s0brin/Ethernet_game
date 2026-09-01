/**
 * lcd.c -- framebuffer primitives and a 5x7 font.
 */
#include "lcd.h"
#include <string.h>

void LCD_Fill(uint16_t c) {
  for (uint32_t i = 0; i < (uint32_t) LCD_W * LCD_H; i++) LCD_FB[i] = c;
}

void LCD_FillRect(int x, int y, int w, int h, uint16_t c) {
  int x1 = x + w, y1 = y + h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x1 > LCD_W) x1 = LCD_W;
  if (y1 > LCD_H) y1 = LCD_H;
  for (int yy = y; yy < y1; yy++) {
    volatile uint16_t *row = LCD_FB + (uint32_t) yy * LCD_W;
    for (int xx = x; xx < x1; xx++) row[xx] = c;
  }
}

void LCD_Frame(int x, int y, int w, int h, int t, uint16_t c) {
  LCD_FillRect(x, y, w, t, c);
  LCD_FillRect(x, y + h - t, w, t, c);
  LCD_FillRect(x, y, t, h, c);
  LCD_FillRect(x + w - t, y, t, h, c);
}

/* -------------------------------------------------------------------------- */
/* 5x7 font. Column-major: each byte is one column, bit 0 = top row.          */
/* -------------------------------------------------------------------------- */

static const char FONT_CHARS[] = " -./0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static const uint8_t FONT_DATA[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  /* space */
    {0x08, 0x08, 0x08, 0x08, 0x08},  /* -     */
    {0x00, 0x60, 0x60, 0x00, 0x00},  /* .     */
    {0x20, 0x10, 0x08, 0x04, 0x02},  /* /     */
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  /* 0     */
    {0x00, 0x42, 0x7F, 0x40, 0x00},  /* 1     */
    {0x42, 0x61, 0x51, 0x49, 0x46},  /* 2     */
    {0x21, 0x41, 0x45, 0x4B, 0x31},  /* 3     */
    {0x18, 0x14, 0x12, 0x7F, 0x10},  /* 4     */
    {0x27, 0x45, 0x45, 0x45, 0x39},  /* 5     */
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  /* 6     */
    {0x01, 0x71, 0x09, 0x05, 0x03},  /* 7     */
    {0x36, 0x49, 0x49, 0x49, 0x36},  /* 8     */
    {0x06, 0x49, 0x49, 0x29, 0x1E},  /* 9     */
    {0x00, 0x36, 0x36, 0x00, 0x00},  /* :     */
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  /* A     */
    {0x7F, 0x49, 0x49, 0x49, 0x36},  /* B     */
    {0x3E, 0x41, 0x41, 0x41, 0x22},  /* C     */
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  /* D     */
    {0x7F, 0x49, 0x49, 0x49, 0x41},  /* E     */
    {0x7F, 0x09, 0x09, 0x09, 0x01},  /* F     */
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  /* G     */
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  /* H     */
    {0x00, 0x41, 0x7F, 0x41, 0x00},  /* I     */
    {0x20, 0x40, 0x41, 0x3F, 0x01},  /* J     */
    {0x7F, 0x08, 0x14, 0x22, 0x41},  /* K     */
    {0x7F, 0x40, 0x40, 0x40, 0x40},  /* L     */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  /* M     */
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  /* N     */
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  /* O     */
    {0x7F, 0x09, 0x09, 0x09, 0x06},  /* P     */
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  /* Q     */
    {0x7F, 0x09, 0x19, 0x29, 0x46},  /* R     */
    {0x46, 0x49, 0x49, 0x49, 0x31},  /* S     */
    {0x01, 0x01, 0x7F, 0x01, 0x01},  /* T     */
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  /* U     */
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  /* V     */
    {0x7F, 0x20, 0x18, 0x20, 0x7F},  /* W     */
    {0x63, 0x14, 0x08, 0x14, 0x63},  /* X     */
    {0x07, 0x08, 0x70, 0x08, 0x07},  /* Y     */
    {0x61, 0x51, 0x49, 0x45, 0x43},  /* Z     */
};

static const uint8_t *font_glyph(char c) {
  if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
  for (unsigned i = 0; i < sizeof(FONT_CHARS) - 1; i++) {
    if (FONT_CHARS[i] == c) return FONT_DATA[i];
  }
  return FONT_DATA[0];
}

void LCD_Text(int x, int y, const char *s, uint16_t fg, int scale) {
  for (; *s != '\0'; s++, x += 6 * scale) {
    const uint8_t *g = font_glyph(*s);
    for (int col = 0; col < 5; col++) {
      if (g[col] == 0) continue;
      for (int row = 0; row < 7; row++) {
        if ((g[col] & (1u << row)) == 0) continue;
        LCD_FillRect(x + col * scale, y + row * scale, scale, scale, fg);
      }
    }
  }
}

int LCD_TextW(const char *s, int scale) {
  return (int) strlen(s) * 6 * scale;
}

void LCD_TextCenter(int y, const char *s, uint16_t fg, int scale) {
  LCD_Text((LCD_W - LCD_TextW(s, scale)) / 2, y, s, fg, scale);
}

void LCD_TextCenterIn(int x, int w, int y, const char *s, uint16_t fg, int scale) {
  LCD_Text(x + (w - LCD_TextW(s, scale)) / 2, y, s, fg, scale);
}
