/**
 * ui.c -- menu screen, game screen, and the hit tests for both.
 */
#include "ui.h"
#include "lcd.h"
#include "touch.h"
#include "main.h"
#include "mongoose.h"

const char *const MODE_NAME[MODE_COUNT] = {
    "LOCAL GAME", "DIRECT CABLE", "ROUTER DHCP"
};
const char *const MODE_HINT[MODE_COUNT] = {
    "2 PLAYERS ON SCREEN", "STATIC 192.168.2.32", "GET ADDRESS FROM ROUTER"
};

/* -------------------------------------------------------------------------- */
/* Menu                                                                       */
/* -------------------------------------------------------------------------- */

#define MENU_X     40
#define MENU_W    400
#define MENU_Y0    70
#define MENU_STEP  58
#define MENU_H     48
#define HOLD_MS   800u

/* Sampled at boot so the button's polarity does not have to be known. */
static GPIO_PinState s_btn_idle;

static bool button_down(void) {
  return HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) != s_btn_idle;
}

static void draw_menu(int sel) {
  LCD_Fill(COL_BG);
  LCD_TextCenter(14, "TIC TAC TOE", COL_TEXT, 3);

  for (int i = 0; i < MODE_COUNT; i++) {
    int y = MENU_Y0 + i * MENU_STEP;
    uint16_t frame = (i == sel) ? COL_ACCENT : COL_LINE;
    uint16_t text  = (i == sel) ? COL_TEXT : COL_DIM;

    LCD_Frame(MENU_X, y, MENU_W, MENU_H, 3, frame);
    LCD_Text(MENU_X + 16, y + 8, MODE_NAME[i], text, 2);
    LCD_Text(MENU_X + 16, y + 28, MODE_HINT[i], COL_DIM, 1);
  }

  LCD_TextCenter(248, "TAP TWICE OR HOLD B1 TO START", COL_DIM, 1);
}

NetMode UI_SelectMode(void) {
  int sel = 0;
  bool was = false, armed = false;
  uint32_t t_down = 0;

  s_btn_idle = HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin);
  draw_menu(sel);
  MG_INFO(("MENU: tap a row twice, or press B1 to cycle and hold to start"));

  for (;;) {
    int tx, ty;
    bool now = button_down();

    if (now && !was) {
      t_down = HAL_GetTick();
      armed  = true;
    }
    if (now && armed && HAL_GetTick() - t_down >= HOLD_MS) {
      armed = false;
      while (button_down()) { }
      return (NetMode) sel;
    }
    if (!now && was && armed) {
      sel = (sel + 1) % MODE_COUNT;
      draw_menu(sel);
      armed = false;
    }
    was = now;

    /* First tap on a row highlights it, a second tap on the same row starts
     * it -- so a stray touch cannot launch a mode by accident. */
    if (Touch_Read(&tx, &ty)) {
      int row = (ty - MENU_Y0) / MENU_STEP;
      if (row >= 0 && row < MODE_COUNT && tx >= MENU_X &&
          tx <= MENU_X + MENU_W && ty >= MENU_Y0) {
        if (row == sel) return (NetMode) sel;
        sel = row;
        draw_menu(sel);
      }
    }
  }
}

/* -------------------------------------------------------------------------- */
/* Game screen                                                                */
/* -------------------------------------------------------------------------- */

bool UI_BackAt(int x, int y) {
  return x >= BACK_X && x < BACK_X + BACK_W &&
         y >= BACK_Y && y < BACK_Y + BACK_H;
}

int UI_CellAt(int x, int y) {
  int col, row;
  if (x < BOARD_X || x >= BOARD_X + BOARD_SIZE) return -1;
  if (y < BOARD_Y || y >= BOARD_Y + BOARD_SIZE) return -1;
  col = (x - BOARD_X) / BOARD_CELL;
  row = (y - BOARD_Y) / BOARD_CELL;
  return row * 3 + col;
}

static void draw_x(int cx, int cy) {
  const int m = 16, t = 6;
  for (int i = 0; i <= BOARD_CELL - 2 * m; i++) {
    LCD_FillRect(cx + m + i - t / 2, cy + m + i - t / 2, t, t, COL_X);
    LCD_FillRect(cx + BOARD_CELL - m - i - t / 2, cy + m + i - t / 2, t, t, COL_X);
  }
}

static void draw_o(int cx, int cy) {
  const int r = BOARD_CELL / 2 - 16, t = 5;
  int c = BOARD_CELL / 2;
  int r2o = (r) * (r), r2i = (r - t) * (r - t);
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      int d = dx * dx + dy * dy;
      if (d <= r2o && d >= r2i) {
        LCD_FillRect(cx + c + dx, cy + c + dy, 1, 1, COL_O);
      }
    }
  }
}

static void draw_back_button(void) {
  LCD_Frame(BACK_X, BACK_Y, BACK_W, BACK_H, 2, COL_LINE);
  LCD_TextCenterIn(BACK_X, BACK_W, BACK_Y + 10, "BACK", COL_DIM, 2);
}

static void draw_status(const Game *g, Cell board_seat, bool waiting) {
  const char *s;
  uint16_t col = COL_TEXT;

  switch (g->status) {
    case GS_WIN_X: s = "X WINS - TAP FOR NEXT ROUND"; col = COL_X; break;
    case GS_WIN_O: s = "O WINS - TAP FOR NEXT ROUND"; col = COL_O; break;
    case GS_DRAW:  s = "DRAW - TAP FOR NEXT ROUND";   col = COL_DIM; break;
    default:
      if (waiting) {
        s   = "WAITING FOR PLAYER";
        col = COL_WARN;
      } else if (board_seat == CELL_EMPTY) {
        /* Local game: the screen plays both sides. */
        s   = (g->current == CELL_X) ? "X TURN" : "O TURN";
        col = (g->current == CELL_X) ? COL_X : COL_O;
      } else if (g->current == board_seat) {
        s   = (board_seat == CELL_X) ? "YOUR TURN - X" : "YOUR TURN - O";
        col = (board_seat == CELL_X) ? COL_X : COL_O;
      } else {
        s   = "OPPONENT IS THINKING";
        col = COL_DIM;
      }
      break;
  }
  LCD_Text(BACK_X + BACK_W + 20, BACK_Y + 10, s, col, 2);
}

static void draw_panel(const Game *g, NetMode mode, const char *info,
                       Cell board_seat) {
  const int px = BOARD_X + BOARD_SIZE + 24;
  const int pw = LCD_W - px - 16;
  char line[24];

  LCD_FillRect(px, BOARD_Y, pw, BOARD_SIZE, COL_PANEL);

  LCD_Text(px + 12, BOARD_Y + 14, "SCORE", COL_DIM, 1);

  mg_snprintf(line, sizeof(line), "X %u", (unsigned) g->score_x);
  LCD_Text(px + 12, BOARD_Y + 34, line, COL_X, 2);
  mg_snprintf(line, sizeof(line), "O %u", (unsigned) g->score_o);
  LCD_Text(px + 12, BOARD_Y + 58, line, COL_O, 2);
  mg_snprintf(line, sizeof(line), "D %u", (unsigned) g->score_draw);
  LCD_Text(px + 12, BOARD_Y + 82, line, COL_DIM, 2);

  mg_snprintf(line, sizeof(line), "ROUND %u", (unsigned) g->round);
  LCD_Text(px + 12, BOARD_Y + 118, line, COL_DIM, 1);

  if (board_seat != CELL_EMPTY) {
    mg_snprintf(line, sizeof(line), "YOU ARE %s",
                (board_seat == CELL_X) ? "X" : "O");
    LCD_Text(px + 12, BOARD_Y + 136, line,
             (board_seat == CELL_X) ? COL_X : COL_O, 1);
  }

  LCD_Text(px + 12, BOARD_Y + 164, MODE_NAME[mode], COL_DIM, 1);
  if (info != NULL) {
    LCD_Text(px + 12, BOARD_Y + 180, info, COL_ACCENT, 1);
  }
}

void UI_DrawGame(const Game *g, NetMode mode, const char *info,
                 Cell board_seat, bool waiting) {
  LCD_Fill(COL_BG);
  draw_back_button();
  draw_status(g, board_seat, waiting);

  /* Grid: two vertical and two horizontal lines inside the board square. */
  for (int i = 1; i <= 2; i++) {
    LCD_FillRect(BOARD_X + i * BOARD_CELL - 1, BOARD_Y, 3, BOARD_SIZE, COL_LINE);
    LCD_FillRect(BOARD_X, BOARD_Y + i * BOARD_CELL - 1, BOARD_SIZE, 3, COL_LINE);
  }

  for (int i = 0; i < 9; i++) {
    int cx = BOARD_X + (i % 3) * BOARD_CELL;
    int cy = BOARD_Y + (i / 3) * BOARD_CELL;
    if (g->board[i] == CELL_X) draw_x(cx, cy);
    else if (g->board[i] == CELL_O) draw_o(cx, cy);
  }

  /* Faint marker on the most recent move. */
  if (g->last_move >= 0) {
    int cx = BOARD_X + (g->last_move % 3) * BOARD_CELL;
    int cy = BOARD_Y + (g->last_move / 3) * BOARD_CELL;
    LCD_FillRect(cx + BOARD_CELL / 2 - 8, cy + BOARD_CELL - 8, 16, 3, COL_ACCENT);
  }

  draw_panel(g, mode, info, board_seat);
}