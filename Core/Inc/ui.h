/**
 * ui.h -- everything drawn on the panel, plus the hit tests that go with it.
 *
 * Layout constants live here so the drawing and the touch hit-testing can
 * never drift apart: both use the same numbers.
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "game.h"

typedef enum {
  MODE_LOCAL = 0,   /* two players on the touchscreen, no network      */
  MODE_DIRECT,      /* straight cable to a laptop, static 192.168.2.32 */
  MODE_ROUTER,      /* plugged into a router, address from DHCP        */
  MODE_COUNT
} NetMode;

extern const char *const MODE_NAME[MODE_COUNT];
extern const char *const MODE_HINT[MODE_COUNT];

/* Board geometry, shared by the renderer and UI_CellAt(). */
#define BOARD_X     32
#define BOARD_Y     46
#define BOARD_CELL  72
#define BOARD_SIZE  (BOARD_CELL * 3)

/* Back button, top left. */
#define BACK_X   6
#define BACK_Y   5
#define BACK_W  86
#define BACK_H  32

/* Blocking menu. Returns when a mode is chosen, by touch or by button B1. */
NetMode UI_SelectMode(void);

/* Full redraw of the game screen.
 *
 *   info        drawn in the side panel, may be NULL -- the IP address in
 *               the network modes
 *   board_seat  which mark this board is allowed to play, or CELL_EMPTY in
 *               a local game where the screen plays both
 *   waiting     true while a network game has no opponent yet */
void UI_DrawGame(const Game *g, NetMode mode, const char *info,
                 Cell board_seat, bool waiting);

/* Screen point -> board cell 0..8, or -1 when outside the board. */
int UI_CellAt(int x, int y);

/* Screen point -> is this the back button? */
bool UI_BackAt(int x, int y);

#endif /* UI_H */