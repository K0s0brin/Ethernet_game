/**
 * game_protocol.c -- JSON state, HTTP endpoints, and the browser page.
 */
#include "game_protocol.h"

static Game *s_game;
static Cell  s_board_seat = CELL_X;
static bool  s_joined;
static bool  s_dirty;

Cell Proto_BoardSeat(void)  { return s_board_seat; }
Cell Proto_RemoteSeat(void) { return Game_Other(s_board_seat); }
bool Proto_RemoteJoined(void) { return s_joined; }

void Proto_SwapSeats(void) {
  s_board_seat = Game_Other(s_board_seat);
}

bool Proto_TakeDirty(void) {
  bool d = s_dirty;
  s_dirty = false;
  return d;
}

/* "" for an empty cell, so the browser can test v == '' */
static const char *mark(Cell c) {
  return (c == CELL_X) ? "X" : (c == CELL_O) ? "O" : "";
}

size_t Proto_StateJson(char *buf, size_t len) {
  char cells[48];
  const char *winner = "null";
  size_t o = 0;

  if (s_game == NULL) return 0;

  for (int i = 0; i < 9; i++) {
    o += mg_snprintf(cells + o, sizeof(cells) - o, "%s\"%s\"",
                     i ? "," : "", mark(s_game->board[i]));
  }

  if (s_game->status == GS_WIN_X) winner = "\"X\"";
  else if (s_game->status == GS_WIN_O) winner = "\"O\"";

  return mg_snprintf(
      buf, len,
      "{\"type\":\"game_state\",\"game\":\"tictactoe\","
      "\"board\":[%s],"
      "\"current_player\":\"%s\","
      "\"status\":\"%s\","
      "\"winner\":%s,"
      "\"move\":%d,"
      "\"round\":%u,"
      "\"you\":\"%s\","
      "\"board_player\":\"%s\","
      "\"score\":{\"x\":%u,\"o\":%u,\"draw\":%u}}",
      cells, mark(s_game->current), Game_StatusName(s_game->status), winner,
      (int) s_game->last_move, (unsigned) s_game->round,
      mark(Proto_RemoteSeat()), mark(s_board_seat),
      (unsigned) s_game->score_x, (unsigned) s_game->score_o,
      (unsigned) s_game->score_draw);
}

/* -------------------------------------------------------------------------- */
/* The page. Single quotes throughout so the C string needs no escaping.      */
/* Polls /api/state every 400 ms -- for tic-tac-toe that is simpler and more  */
/* robust than a WebSocket, and costs no extra flash.                         */
/* -------------------------------------------------------------------------- */

static const char HTML_PAGE[] =
    "<!DOCTYPE html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>STM32 Tic Tac Toe</title><style>"
    "body{background:#0b1220;color:#e6edf3;font:16px system-ui,sans-serif;"
    "display:flex;flex-direction:column;align-items:center;padding:28px}"
    "h1{font-size:19px;letter-spacing:.14em;margin:0 0 6px;font-weight:600}"
    "#s{color:#8b98a5;margin:0 0 18px;min-height:22px;letter-spacing:.06em}"
    "#b{display:grid;grid-template-columns:repeat(3,92px);gap:6px}"
    ".c{height:92px;font-size:42px;font-weight:700;background:#151d2b;"
    "color:#e6edf3;border:2px solid #2b3a4f;border-radius:8px;cursor:pointer}"
    ".c:disabled{cursor:default;opacity:.9}"
    ".x{color:#60c4ff}.o{color:#ff8c5a}"
    "#n{margin-top:20px;padding:9px 20px;background:#00dc82;color:#04150c;"
    "border:0;border-radius:6px;font-weight:700;letter-spacing:.08em;"
    "cursor:pointer}"
    "#f{color:#5c6b7a;font-size:12px;margin-top:18px;letter-spacing:.06em}"
    "</style></head><body>"
    "<h1>TIC TAC TOE</h1><p id=s>connecting</p><div id=b></div>"
    "<button id=n>NEW ROUND</button><p id=f></p>"
    "<script>"
    "const b=document.getElementById('b'),s=document.getElementById('s'),"
    "f=document.getElementById('f'),cells=[];"
    "for(let i=0;i<9;i++){const e=document.createElement('button');"
    "e.className='c';e.onclick=()=>post('/api/move',{cell:i});"
    "b.appendChild(e);cells.push(e);}"
    "document.getElementById('n').onclick=()=>post('/api/new',{});"
    "function post(u,d){fetch(u,{method:'POST',body:JSON.stringify(d)})"
    ".then(r=>r.json()).then(draw);}"
    "function draw(g){"
    "for(let i=0;i<9;i++){const v=g.board[i];const c=cells[i];"
    "c.textContent=v;c.className='c'+(v=='X'?' x':v=='O'?' o':'');"
    "c.disabled=v!=''||g.status!='playing'||g.current_player!=g.you;}"
    "s.textContent=g.status=='won'?g.winner+' WINS':"
    "g.status=='draw'?'DRAW':"
    "g.current_player==g.you?'YOUR TURN':'WAITING FOR THE BOARD';"
    "f.textContent='you are '+g.you+'   round '+g.round+"
    "'   X '+g.score.x+'  O '+g.score.o+'  draw '+g.score.draw;}"
    "setInterval(()=>fetch('/api/state').then(r=>r.json()).then(draw),400);"
    "</script></body></html>";

/* -------------------------------------------------------------------------- */
/* HTTP                                                                       */
/* -------------------------------------------------------------------------- */

#define JSON_HDR "Content-Type: application/json\r\n"

static void reply_state(struct mg_connection *c) {
  char buf[360];
  Proto_StateJson(buf, sizeof(buf));
  mg_http_reply(c, 200, JSON_HDR, "%s", buf);
}

static void http_ev(struct mg_connection *c, int ev, void *ev_data) {
  struct mg_http_message *hm = (struct mg_http_message *) ev_data;

  if (ev != MG_EV_HTTP_MSG) return;

  if (mg_strcmp(hm->uri, mg_str("/api/state")) == 0) {
    if (!s_joined) {
      s_joined = true;
      s_dirty  = true;                    /* redraw: opponent has arrived */
      MG_INFO(("NET: browser joined, it plays %s", mark(Proto_RemoteSeat())));
    }
    reply_state(c);

  } else if (mg_strcmp(hm->uri, mg_str("/api/move")) == 0) {
    long cell = mg_json_get_long(hm->body, "$.cell", -1);
    s_joined = true;
    if (cell >= 0 && cell < 9 &&
        Game_Move(s_game, (uint8_t) cell, Proto_RemoteSeat())) {
      s_dirty = true;
      MG_INFO(("MOVE: player=%s cell=%ld status=%s (browser)",
               mark(s_game->board[cell]), cell,
               Game_StatusName(s_game->status)));
    }
    reply_state(c);

  } else if (mg_strcmp(hm->uri, mg_str("/api/new")) == 0) {
    s_joined = true;
    App_NewRound();
    s_dirty = true;
    reply_state(c);

  } else {
    mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", HTML_PAGE);
  }
}

void Proto_Start(struct mg_mgr *mgr, Game *g, Cell board_seat) {
  s_game       = g;
  s_board_seat = board_seat;
  s_joined     = false;
  s_dirty      = false;

  if (mg_http_listen(mgr, "http://0.0.0.0:80", http_ev, NULL) != NULL) {
    MG_INFO(("HTTP listening on port 80, board plays %s", mark(s_board_seat)));
  } else {
    MG_ERROR(("HTTP listen failed"));
  }
}
