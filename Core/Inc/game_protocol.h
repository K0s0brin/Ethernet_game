/**
 * game_protocol.h -- JSON over HTTP, and the web UI the browser loads.
 *
 * Seats
 * -----
 * The board holds one mark, the browser holds the other. Round 1 the board
 * is X and opens; on every new round the two swap seats, so the browser
 * opens round 2, the board round 3, and so on. X always moves first.
 *
 * Every move -- from the touchscreen or from the browser -- goes through
 * Game_Move(), which refuses a move that is not the caller's turn. That is
 * the whole of the turn enforcement: the board cannot play the browser's
 * mark and the browser cannot play the board's, because neither is ever
 * handed the other's seat.
 *
 * Endpoints
 * ---------
 *   GET  /            the page
 *   GET  /api/state   current game state as JSON
 *   POST /api/move    {"cell":0..8}, applied as the remote seat
 *   POST /api/new     start the next round
 */
#ifndef GAME_PROTOCOL_H
#define GAME_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include "game.h"
#include "mongoose.h"

/* Starts the HTTP listener on port 80 and binds it to this game. */
void Proto_Start(struct mg_mgr *mgr, Game *g, Cell board_seat);

/* Which mark each side is playing. */
Cell Proto_BoardSeat(void);
Cell Proto_RemoteSeat(void);
void Proto_SwapSeats(void);

/* True once a browser has talked to us at all. Until then the board is
 * waiting for an opponent. */
bool Proto_RemoteJoined(void);

/* True once after a remote move or a remote new-round, so the main loop
 * knows to redraw. Clears the flag. */
bool Proto_TakeDirty(void);

/* Serialise the current state. Used by the HTTP layer and available for the
 * raw TCP port on 12345. Returns the number of bytes written. */
size_t Proto_StateJson(char *buf, size_t len);

/*
 * Implemented in main.c. Starts the next round using the rules for the
 * current mode, so the browser's "new round" button and a tap on the panel
 * do exactly the same thing.
 */
void App_NewRound(void);

#endif /* GAME_PROTOCOL_H */
