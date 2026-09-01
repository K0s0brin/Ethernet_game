/**
 * game.h -- Tic-Tac-Toe rules.
 *
 * No hardware, no LCD, no network. This compiles and can be tested on a PC
 * with nothing but a C compiler. Both the touchscreen and the HTTP handler
 * go through Game_Move(), so there is exactly one place where a move is
 * validated and applied.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>

typedef enum { CELL_EMPTY = 0, CELL_X, CELL_O } Cell;

typedef enum {
  GS_PLAYING = 0,   /* a move is expected from g->current */
  GS_WIN_X,
  GS_WIN_O,
  GS_DRAW
} GameStatus;

typedef struct {
  Cell       board[9];      /* index 0..8, row major */
  Cell       current;        /* whose turn                        */
  Cell       starter;        /* who opened this round             */
  GameStatus status;
  uint8_t    move_count;     /* 0..9                              */
  int8_t     last_move;      /* 0..8, -1 when none                */
  uint16_t   round;          /* increments on every rematch       */
  uint32_t   score_x, score_o, score_draw;
} Game;

/* New game, scores cleared, X opens. */
void Game_Init(Game *g);

/* Clear the board, keep the same opener, keep the scores. */
void Game_Reset(Game *g);

/* Next round: clears the board, bumps `round`, and sets who opens.
 * The caller decides: in a local game the opener alternates, in a network
 * game X always opens and the two players swap seats instead. */
void Game_NewRound(Game *g, Cell starter);

/* Apply `player`'s move on `cell`. Returns false and changes nothing if the
 * game is over, the cell is out of range or taken, or it is not that
 * player's turn. On a win or draw the score is updated here. */
bool Game_Move(Game *g, uint8_t cell, Cell player);

/* Recompute the status from the board. Does not modify the game. */
GameStatus Game_CheckWin(const Game *g);

/* "X" / "O" / " " and "playing" / "won" / "draw" for the JSON layer. */
const char *Game_CellName(Cell c);
const char *Game_StatusName(GameStatus s);
Cell        Game_Other(Cell c);

#endif /* GAME_H */