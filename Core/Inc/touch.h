/**
 * touch.h -- capacitive touch controller on I2C4 (PD12 = SCL, PD13 = SDA).
 *
 * The board shares that bus with the WM8994 audio codec at 0x1A. The touch
 * controller's address is discovered by scanning, so no address is hard-coded.
 */
#ifndef TOUCH_H
#define TOUCH_H

#include <stdbool.h>
#include <stdint.h>

/* Brings up I2C4, pulses the board reset line, scans the bus and identifies
 * the touch controller. Logs everything it finds. Safe to call once at boot. */
void Touch_Init(void);

/* True once per press, with coordinates already mapped to the panel
 * (0..479 x 0..271). Returns false while no finger is down and for every
 * poll after the first of the same press. */
bool Touch_Read(int *x, int *y);

/* True while a finger is down, regardless of press edges. */
bool Touch_IsDown(void);

#endif /* TOUCH_H */
