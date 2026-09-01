/**
 * touch.c -- FocalTech-style capacitive touch controller over I2C4.
 */
#include "touch.h"
#include "main.h"
#include "mongoose.h"
#include "lcd.h"

/* -------------------------------------------------------------------------- */
/* I2C4                                                                        */
/*                                                                            */
/* PD12 = I2C4_SCL, PD13 = I2C4_SDA, both AF4. Timing 0x00602173 was computed */
/* by CubeMX for Fast Mode 400 kHz at a 64 MHz I2C4 kernel clock -- recompute */
/* it if the clock tree changes.                                              */
/* -------------------------------------------------------------------------- */

static I2C_HandleTypeDef hi2c4;
static uint8_t s_addr7;      /* touch controller, 0 = not found */
static bool    s_ok;

/* Overrides the __weak version in stm32h7xx_hal_i2c.c. Clock only; the pins
 * are configured in i2c_pins() so the scanner can try more than one pair. */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c) {
  if (hi2c->Instance != I2C4) return;
  __HAL_RCC_I2C4_CLK_ENABLE();
}

static void i2c_pins(GPIO_TypeDef *sclp, uint16_t sclpin,
                     GPIO_TypeDef *sdap, uint16_t sdapin) {
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  g.Mode      = GPIO_MODE_AF_OD;          /* I2C is open drain */
  g.Pull      = GPIO_PULLUP;
  g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF4_I2C4;
  g.Pin = sclpin;
  HAL_GPIO_Init(sclp, &g);
  g.Pin = sdapin;
  HAL_GPIO_Init(sdap, &g);
}

static void i2c_init(void) {
  hi2c4.Instance              = I2C4;
  hi2c4.Init.Timing           = 0x00602173;
  hi2c4.Init.OwnAddress1      = 0;
  hi2c4.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
  hi2c4.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
  hi2c4.Init.OwnAddress2      = 0;
  hi2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c4.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
  hi2c4.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

  HAL_I2C_DeInit(&hi2c4);
  if (HAL_I2C_Init(&hi2c4) != HAL_OK) Error_Handler();
  HAL_I2CEx_ConfigAnalogFilter(&hi2c4, I2C_ANALOGFILTER_ENABLE);
  HAL_I2CEx_ConfigDigitalFilter(&hi2c4, 0);
}

/*
 * PB12 is labelled LCD_RST but also holds the touch controller in reset.
 * MX_GPIO_Init() configures it as an output and never writes it, so it sits
 * low and the touch chip never answers. FocalTech parts need roughly
 * 100-200 ms after the release before they respond on I2C.
 */
static void reset_pulse(void) {
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(20);
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(200);
}

/* -------------------------------------------------------------------------- */
/* Touch controller registers (FocalTech FT5xxx / FT6xxx layout)              */
/* -------------------------------------------------------------------------- */

#define TS_REG_TD      0x02   /* low nibble = number of active points */
#define TS_REG_P1XH    0x03   /* P1_XH, P1_XL, P1_YH, P1_YL           */
#define TS_REG_CHIPID  0xA3
#define TS_REG_VENDID  0xA8

static bool ts_rd(uint8_t reg, uint8_t *buf, uint16_t n) {
  return HAL_I2C_Mem_Read(&hi2c4, (uint16_t) (s_addr7 << 1), reg,
                          I2C_MEMADD_SIZE_8BIT, buf, n, 20) == HAL_OK;
}

void Touch_Init(void) {
  uint8_t id = 0, vend = 0;
  int found = 0;

  s_ok    = false;
  s_addr7 = 0;

  i2c_pins(GPIOD, GPIO_PIN_12, GPIOD, GPIO_PIN_13);
  i2c_init();
  reset_pulse();

  MG_INFO(("I2C4 scan on PD12/PD13..."));
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    if (HAL_I2C_IsDeviceReady(&hi2c4, (uint16_t) (a << 1), 2, 5) != HAL_OK) {
      continue;
    }
    MG_INFO(("  device at 0x%02x", (unsigned) a));
    found++;
    /* 0x1A je WM8994 audio kodek ki sharea bus. kar je drugo je naša naprava*/
    if (a != 0x1A && s_addr7 == 0) s_addr7 = a;
  }

  if (found == 0) {
    MG_ERROR(("I2C4: nothing on the bus"));
    return;
  }
  if (s_addr7 == 0) {
    MG_ERROR(("I2C4: only the audio codec answered, no touch controller"));
    return;
  }

  if (!ts_rd(TS_REG_CHIPID, &id, 1) || !ts_rd(TS_REG_VENDID, &vend, 1)) {
    MG_ERROR(("TS: 0x%02x answered the scan but not a register read",
              (unsigned) s_addr7));
    return;
  }

  s_ok = true;
  MG_INFO(("TS: addr 0x%02x, chip id 0x%02x, vendor 0x%02x",
           (unsigned) s_addr7, (unsigned) id, (unsigned) vend));
}

/* Kontr- */
static bool read_raw(uint16_t *x, uint16_t *y) {
  uint8_t n = 0, d[4];
  if (!s_ok) return false;
  if (!ts_rd(TS_REG_TD, &n, 1)) return false;
  if ((n & 0x0F) == 0) return false;
  if (!ts_rd(TS_REG_P1XH, d, 4)) return false;
  *x = (uint16_t) (((d[0] & 0x0F) << 8) | d[1]);
  *y = (uint16_t) (((d[2] & 0x0F) << 8) | d[3]);
  return true;
}

bool Touch_IsDown(void) {
  uint16_t x, y;
  return read_raw(&x, &y);
}

/*
 * raw y tracks screen x over 0..479, raw x tracks screen y over 0..271, and
 * neither axis is mirrored. The controller is simply mounted rotated 90
 * degrees relative to the panel, so the transform is a plain swap.
 */
bool Touch_Read(int *x, int *y) {
  static bool held;
  uint16_t rx, ry;

  if (!read_raw(&rx, &ry)) {
    held = false;
    return false;
  }
  if (held) return false;              
  held = true;

  *x = (int) ry;
  *y = (int) rx;
  if (*x < 0) *x = 0;
  if (*y < 0) *y = 0;
  if (*x >= LCD_W) *x = LCD_W - 1;
  if (*y >= LCD_H) *y = LCD_H - 1;
  return true;
}
