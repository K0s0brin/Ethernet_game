/**
 * game.c -- Tic-Tac-Toe rules. No hardware, no LCD, no network.
 */
#include "game.h"

static const uint8_t WIN_LINES[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},   /* rows      */
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8},   /* columns   */
    {0, 4, 8}, {2, 4, 6},              /* diagonals */
};

Cell Game_Other(Cell c) {
  return (c == CELL_X) ? CELL_O : CELL_X;
}

static void clear_board(Game *g) {
  for (uint8_t i = 0; i < 9; i++) g->board[i] = CELL_EMPTY;
  g->current    = g->starter;
  g->status     = GS_PLAYING;
  g->move_count = 0;
  g->last_move  = -1;
}

void Game_Init(Game *g) {
  g->starter    = CELL_X;
  g->round      = 1;
  g->score_x    = 0;
  g->score_o    = 0;
  g->score_draw = 0;
  clear_board(g);
}

void Game_Reset(Game *g) {
  clear_board(g);
}

void Game_NewRound(Game *g, Cell starter) {
  g->starter = (starter == CELL_EMPTY) ? g->starter : starter;
  g->round++;
  clear_board(g);
}

GameStatus Game_CheckWin(const Game *g) {
  for (uint8_t i = 0; i < 8; i++) {
    Cell a = g->board[WIN_LINES[i][0]];
    if (a == CELL_EMPTY) continue;
    if (a == g->board[WIN_LINES[i][1]] && a == g->board[WIN_LINES[i][2]]) {
      return (a == CELL_X) ? GS_WIN_X : GS_WIN_O;
    }
  }
  for (uint8_t i = 0; i < 9; i++) {
    if (g->board[i] == CELL_EMPTY) return GS_PLAYING;
  }
  return GS_DRAW;
}

bool Game_Move(Game *g, uint8_t cell, Cell player) {
  if (g->status != GS_PLAYING)      return false;   /* round is over    */
  if (cell > 8)                     return false;   /* off the board    */
  if (g->board[cell] != CELL_EMPTY) return false;   /* occupied         */
  if (player != g->current)         return false;   /* not your turn    */

  g->board[cell] = player;
  g->last_move   = (int8_t) cell;
  g->move_count++;

  g->status = Game_CheckWin(g);

  switch (g->status) {
    case GS_WIN_X: g->score_x++;    break;
    case GS_WIN_O: g->score_o++;    break;
    case GS_DRAW:  g->score_draw++; break;
    default:       g->current = Game_Other(player); break;
  }

  return true;
}

const char *Game_CellName(Cell c) {
  return (c == CELL_X) ? "X" : (c == CELL_O) ? "O" : " ";
}

const char *Game_StatusName(GameStatus s) {
  switch (s) {
    case GS_PLAYING: return "playing";
    case GS_WIN_X:   return "won";
    case GS_WIN_O:   return "won";
    case GS_DRAW:    return "draw";
    default:         return "unknown";
  }
}