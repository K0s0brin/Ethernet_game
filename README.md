
Na mizi je plošča STM32H750B-DK. Ima zaslon na dotik in Ethernet vtičnico.
Cilj: na zaslonu igraš tic tac toe, hkrati pa se lahko nekdo z druge naprave v istem podomrežju se poveže z brskalnikom in igra proti plošči. Plošča je edini lastnik
stanja igre zaslon in brskalnik sta samo dva pogleda na isto stvar.

## Kaj rabimo ##

| Plošča            | STM32H750B-DK                                      |
| ----------------- | -------------------------------------------------- |
| MCU               | STM32H750XBH6, Cortex-M7, 128 KB flasha            |
| Zaslon            | RK043FN48H, 480 × 272                              |
| Dotik             | FT5336 FocalTech na I2C4 vodilu                    |
| Ethernet          | LAN8740A, MII, MDIO naslov 1                       |
| Orodja            | VS Code, CMake + ninja, arm-none-eabi-gcc, ST-LINK |
| Za razhroščevanje | Serial Monitor, Wireshark, `ping`, `arp`           |

## Začnimo: zaslon

Slikovni pomnilnik damo kar v AXI SRAM:

```c
#define LCD_W  480
#define LCD_H  272
#define LCD_FB ((volatile uint16_t *) 0x24000000)   /* AXI SRAM */
```

480 × 272 × 2 bajta = 261 120 B. AXI SRAM ima 512 KB in bi sicer sameval prazen.
S tem si prihranimo **celoten zunanji SDRAM** — zagonsko sekvenco JEDEC, nastavljanje
ure FMC, vse. Ena vrstica namesto pol dneva dela.

Dva bajta na piksel pomeni RGB565.

```text
vmesnik LTDC = RGB888   ->  koliko pinov je fizično priklopljenih (kako je plošča vezana)
format sloja = RGB565   ->  koliko bajtov na piksel je v pomnilniku
```

Panel ima 24 podatkovnih linij, torej je vmesnik RGB888. To pa ne pomeni, da moramo
za vsak piksel porabiti tri bajte — v pomnilniku držimo RGB565, pretvorbo v 8/8/8 na
pinih pa opravi LTDC sam. Prihranili smo 130 KB.

Časovni parametri panela iz podatkovnega lista (HSYNC 41, HBP 13, HFP 32, VSYNC 10,
VBP 2, VFP 2)

```c
hltdc.Init.HorizontalSync     = 40;  
hltdc.Init.VerticalSync       = 9;    
hltdc.Init.AccumulatedHBP     = 53;    
hltdc.Init.AccumulatedVBP     = 11;
hltdc.Init.AccumulatedActiveW = 533;   
hltdc.Init.AccumulatedActiveH = 283;   
hltdc.Init.TotalWidth         = 565;   
hltdc.Init.TotalHeigh         = 285;  

pLayerCfg.PixelFormat   = LTDC_PIXEL_FORMAT_RGB565;
pLayerCfg.FBStartAdress = 0x24000000;
```

### Ura za piksle

Pikslovna ura pride iz PLL3. Panel hoče okoli 9,6 MHz:

```c
PeriphClkInit.PLL3.PLL3M = 5;
PeriphClkInit.PLL3.PLL3N = 160;      /* 5 MHz * 160 = 800 MHz VCO */
PeriphClkInit.PLL3.PLL3R = 83;       /* 800 / 83 = 9,63 MHz       */
PeriphClkInit.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
PeriphClkInit.PLL3.PLL3RGE    = RCC_PLL3VCIRANGE_2;
```

566 taktov  ×  286 vrstic  ×  60 okvirjev/s  =  9,7 MHz

### Risanje brez BSP

ST-jev BSP zna risati pisave, gumbe in vse ostalo, ampak stane okoli 12 KB flasha in
prinese s seboj deset datotek. Napišimo raje svojo pisavo 5 × 7, kjer je vsak znak pet bajtov — en bajt na stolpec, en bit na piksel:

```c
void LCD_Text(int x, int y, const char *s, uint16_t fg, int scale) {
  for (; *s != '\0'; s++, x += 6 * scale) {     /* 5 px znak + 1 px razmik */
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
```

41 znakov (velike črke, številke, nekaj ločil) = okoli 200 bajtov. Parameter scale
nam da poljubno velikost brez druge tabele. 

![[Pasted image 20260901191649.png]]

## Dotik

Zaslon zdaj nekaj kaže. Zdaj hočemo, da se odziva.

### Kje sploh je krmilnik?

Krmilnik dotika je na I2C4, pina PD12 (SCL) in PD13 (SDA), pri jedrni uri 64 MHz je
Timing = 0x00602173 (Fast Mode, 400 kHz, CubeMX). Napišemo skener, ki prevozi celo vodilo ker je veliko bolje videti, kaj tam **je**, kot ugibati, kaj bi moralo biti:

```c
for (uint8_t a = 0x08; a <= 0x77; a++) {
  if (HAL_I2C_IsDeviceReady(&hi2c4, (uint16_t)(a << 1), 2, 5) != HAL_OK) continue;
  MG_INFO(("  naprava na 0x%02x", (unsigned) a));
  if (a != 0x1A && s_addr7 == 0) s_addr7 = a;   /* 0x1A je zvočni kodek WM8994 */
}
```

Prvi zagon skenerja javi samo 0x1a. To je zvočni kodek, ki visi na istem vodilu.
Krmilnik dotika molči — in molči zato, ker ga nihče ni spustil iz ponastavitve.
Pin PB12 je v CubeMX označen kot LCD_RST, v resnici pa drži v ponastavitvi tudi
krmilnik dotika. MX_GPIO_Init() ga nastavi kot izhod in vanj nikoli ne piše, torej
ostane na nizkem stanju za vedno.

```c
static void reset_pulse(void) {
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(20);
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(200);              /* FocalTech potrebuje ~100-200 ms */
}
```

Zdaj skener javi `0x1a` in `0x38`. Tam je.

### X in Y os

Zdaj lahko beremo koordinate, samo ne vemo še, kaj pomenijo. Namesto da bi prepisovali
iz tuje kode, napišimo funkcijo, ki surove vrednosti samo izpisuje, in se dotaknimo
štirih vogalov:

```
TOUCH raw x=51  y=48     <- zgoraj levo
TOUCH raw x=38  y=466    <- zgoraj desno
TOUCH raw x=259 y=37     <- spodaj levo
TOUCH raw x=241 y=453    <- spodaj desno
```

Nobena os ni zrcaljena. Krmilnik je preprosto vgrajen zasukan za 90°, preslikava pa je čista zamenjava:

```c
bool Touch_Read(int *x, int *y) {
  static bool held;
  uint16_t rx, ry;

  if (!read_raw(&rx, &ry)) { held = false; return false; }
  if (held) return false;              /* en dogodek na pritisk */
  held = true;

  *x = (int) ry;                       /* krmilnikov Y -> zaslonski X */
  *y = (int) rx;                       /* krmilnikov X -> zaslonski Y */
  return true;
}
```

## Igra

Logika igre je v game.c 

```c
typedef struct {
  Cell       board[9];
  Cell       current;      /* kdo je na potezi   */
  Cell       starter;      /* kdo je začel rundo */
  GameStatus status;       /* GS_PLAYING, GS_WIN_X, GS_WIN_O, GS_DRAW */
  uint8_t    move_count;
  int8_t     last_move;
  uint16_t   round;
  uint32_t   score_x, score_o, score_draw;
} Game;
```

Cela igra je ena struktura. In cela igra ima samo ena vrata, skozi katera gre
poteza:

```c
bool Game_Move(Game *g, uint8_t cell, Cell player) {
  if (g->status != GS_PLAYING)      return false;   /* runda je končana */
  if (cell > 8)                     return false;   /* izven plošče     */
  if (g->board[cell] != CELL_EMPTY) return false;   /* zasedeno polje   */
  if (player != g->current)         return false;   /* ni tvoja poteza  */

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
```
## Ethernet

PHY na tej plošči je z mikrokrmilnikom povezan prek MII — Media Independent Interface. To je vzporedno vodilo s 15 linijami, in prav toliko pinov moramo konfigurirati:

| Skupina     | Linije                                    | Kaj počne                                 |
| ----------- | ----------------------------------------- | ----------------------------------------- |
| oddajanje   | `TX_CLK`, `TXD0`–`TXD3`, `TX_EN`          | 4 biti podatkov na takt, ura pride iz PHY |
| sprejemanje | `RX_CLK`, `RXD0`–`RXD3`, `RX_DV`, `RX_ER` | isto v drugo smer, plus zastavica napake  |
| upravljanje | `MDC`, `MDIO`                             | ločeno počasno vodilo za registre PHY     |

Podatki tečejo po štirih linijah naenkrat, ura je 25 MHz. 4 biti × 25 MHz = 100 Mbit/s — točno hitrost povezave. Ure ne generira STM32, ampak jo dobi od PHY-ja: TX_CLK in RX_CLK sta vhoda.

## MAC 

Naslov MAC izpeljemo iz serijske številke čipa, da dve plošči na istem omrežju ne
trčita:

```c
static void mac_from_uid(void) {
  const uint32_t *uid = (const uint32_t *) 0x1FF1E800;
  s_mac[0] = 0x02;                            
  s_mac[1] = (uint8_t) (uid[0] & 0xFF);
  s_mac[2] = (uint8_t) ((uid[0] >> 10) & 0xFF);
  s_mac[3] = (uint8_t) ((uid[0] >> 19) & 0xFF);
  s_mac[4] = (uint8_t) (uid[1] & 0xFF);
  s_mac[5] = (uint8_t) (uid[2] & 0xFF);
}
```

## Mongoose in gonilnik vmes

Uporabi se TCP/IP sklad iz knjižice Mongoose, vklopimo ga z
MG_ENABLE_TCPIP 1. Od nas hoče gonilnik s štirimi funkcijami:

```c
static struct mg_tcpip_driver s_drv = {
    .init = drv_init,   /* HAL_ETH_Start()                    */
    .tx   = drv_tx,     /* pošlji okvir                       */
    .rx   = drv_rx,     /* daj mi okvir, če ga imaš           */
    .poll = drv_poll,   /* stanje povezave, enkrat na sekundo */
};
```
### DMA ne vidi DTCM

Mongoosov izhodni medpomnilnik leži v .bss, torej v DTCM. Ethernet DMA je na vodil AHB in do DTCM nima dostopa. Ne javi napake, samo prebere smeti. Rešitev je prepis v pomnilnik, ki ga DMA vidi:

```c
/* Izhodni medpomnilnik knjižnice je v DTCM, kamor DMA nima dostopa. */
static uint8_t s_tx_bounce[1600] __attribute__((section(".EthBufSection")));

static size_t drv_tx(const void *buf, size_t len, struct mg_tcpip_if *ifp) {
  ETH_BufferTypeDef  txbuf;
  ETH_TxPacketConfig txcfg;
  if (buf == NULL || len == 0 || len > sizeof(s_tx_bounce)) return 0;

  memcpy(s_tx_bounce, buf, len);      /* <- cela poanta te funkcije */

  txbuf.buffer = s_tx_bounce;
  txbuf.len    = len;
  txbuf.next   = NULL;
  /* ... txcfg ... */
  return HAL_ETH_Transmit(&heth, &txcfg, 100) == HAL_OK ? len : 0;
}

static size_t drv_rx(void *buf, size_t len, struct mg_tcpip_if *ifp) {
  void *pbuf = NULL;
  if (HAL_ETH_ReadData(&heth, &pbuf) != HAL_OK || pbuf == NULL) return 0;
  size_t n = heth.RxDescList.RxDataLength;
  if (n > len) n = len;
  memcpy(buf, pbuf, n);
  return n;
}
```

.EthBufSection je v povezovalni skripti v RAM_D2 na 0x30000000. Tam so tudi
deskriptorji DMA in sprejemni medpomnilniki, in MPU to celo območje označi kot
nepredpomnjeno:

```c
MPU_InitStruct.BaseAddress  = 0x30000000;
MPU_InitStruct.Size         = MPU_REGION_SIZE_1MB;
MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
MPU_InitStruct.IsCacheable  = MPU_ACCESS_NOT_CACHEABLE;
MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
```

Še ena stvar: Mongoose ima **svoj** gonilnik za STM32H7 (mg_tcpip_driver_stm32h).
Ta nastavlja SYSCFG na RMII, torej je na tej plošči neuporaben. Izklopimo ga z
MG_ENABLE_DRIVER_STM32H 0.

### Naslov: statični ali od routerja

Mongoosu prazen naslov pomeni "zaženi odjemalca DHCP". To je celotna logika preklopa:

```c
if (s_mode == MODE_ROUTER) {
  mif->ip = mif->mask = mif->gw = 0;      /* prazno -> vklopi se DHCP */
} else {
  mif->ip   = mg_htonl(MG_U32(192, 168, 2, 32));
  mif->mask = mg_htonl(MG_U32(255, 255, 255, 0));
  mif->gw   = mg_htonl(MG_U32(192, 168, 2, 1));
}

mif->driver          = &s_drv;
mif->recv_queue.size = 8192;   /* brez tega bo vrsta velika en sam okvir! */

mg_tcpip_init(mgr, mif);
```

Omrežje zaganjamo šele po izbiri načina, ne ob zagonu. Tako nam ni treba
nikoli ponovno inicializirati sklada.

Naslov, ki ga plošča dobi, izpišemo na zaslon. Pri DHCP je to edini način, da veš,
kam usmeriti brskalnik.

## ARP, ping, TCP — po vrsti

Vsak sloj ima svoj test in svoj izpis.

Uspešen zagon:

```text
BOOT, HCLK=64000000 Hz
LCD OK 480x272 @ 0x24000000
I2C4 scan: device at 0x1a, device at 0x38
TS: FocalTech at 0x38
MODE: DIRECT CABLE
PHY at addr 1, ID 0007:c111
LINK UP: 100M FULL
MAC 02:3a:5c:11:47:9b
READY, IP: 192.168.2.32
Listening on TCP/12345
HTTP on port 80, board plays X
```

Na Windows strani:

```powershell
ping 192.168.2.32  
```

In v Wiresharku po vrsti:

| Kaj vidiš | Kaj to pomeni |
|---|---|
| `ARP Request  Who has 192.168.2.32?` | računalnik nas išče |
| `ARP Reply  192.168.2.32 is at 02:...` | sloj 2 dela v obe smeri |
| `ICMP Echo request / reply` | IPv4 in ICMP delata |
| `TCP SYN, SYN-ACK, ACK` | povezava TCP je vzpostavljena |
| `HTTP GET /api/state -> 200 OK` | strežnik odgovarja z JSON |

## JSON

En medpomnilnik fiksne velikosti, brez malloc.

```c
size_t Proto_StateJson(char *buf, size_t len, Cell you) {
  char cells[48];
  const char *winner = "null";
  size_t o = 0;

  for (int i = 0; i < 9; i++)
    o += mg_snprintf(cells + o, sizeof(cells) - o, "%s\"%s\"",
                     i ? "," : "", mark(s_game->board[i]));

  if (s_game->status == GS_WIN_X) winner = "\"X\"";
  else if (s_game->status == GS_WIN_O) winner = "\"O\"";

  return mg_snprintf(buf, len,
      "{\"type\":\"game_state\",\"game\":\"tictactoe\","
      "\"board\":[%s],\"current_player\":\"%s\",\"status\":\"%s\","
      "\"winner\":%s,\"move\":%d,\"round\":%u,"
      "\"you\":\"%s\",\"board_player\":\"%s\",\"seats_open\":%d,"
      "\"score\":{\"x\":%u,\"o\":%u,\"draw\":%u}}",
      cells, mark(s_game->current), Game_StatusName(s_game->status), winner,
      (int) s_game->last_move, (unsigned) s_game->round,
      mark(you), mark(s_board_seat), Proto_SeatsOpen(),
      (unsigned) s_game->score_x, (unsigned) s_game->score_o,
      (unsigned) s_game->score_draw);
}
```

Izpis:

```json
{"type":"game_state","game":"tictactoe",
 "board":["X","","O","","X","","O","",""],
 "current_player":"O","status":"playing","winner":null,
 "move":4,"round":1,"you":"O","board_player":"X","seats_open":0,
 "score":{"x":0,"o":0,"draw":0}}
```

Polje you je najbolj zanimivo. Vsak odjemalec dobi **svoj** odgovor, v katerem piše,
kateri znak je njegov. Brskalnik zato ne rabi ugibati nasprotnikove celice preprosto
onemogoči, namesto da bi klik poslal in dobil zavrnitev.

![[Pasted image 20260901194024.png]]

### TCP ali HTTP?

Pomembno razlikovanje, ki se ga rado zamenja:

```text
TCP
 +-- poljubno sporočilo JSON        <- vrata 12345

TCP
 +-- HTTP
      +-- JSON                      <- vrata 80, za brskalnik
```

Brskalnik **ne zna** brati poljubnega toka JSON po TCP. Za brskalnik rabiš HTTP.

Imamo torej oboje: vrata 12345 ostanejo odprta za surovo pošiljanje, vrata 80 pa strežejo stran in API.

## Spletna stran v brskalniku

Mongoosov strežnik HTTP, tri končne točke in ena stran, vgrajena kot niz v C:

```c
static void http_ev(struct mg_connection *c, int ev, void *ev_data) {
  struct mg_http_message *hm = (struct mg_http_message *) ev_data;
  char key[SEAT_KEY_LEN] = {0};
  Cell you;

  if (ev != MG_EV_HTTP_MSG) return;

  if (mg_http_get_var(&hm->query, "id", key, sizeof(key)) <= 0) key[0] = '\0';
  you = seat_for(key);                    /* ključ -> sedež tega odjemalca */

  if (mg_strcmp(hm->uri, mg_str("/api/state")) == 0) {
    reply_state(c, you);

  } else if (mg_strcmp(hm->uri, mg_str("/api/move")) == 0) {
    long cell = mg_json_get_long(hm->body, "$.cell", -1);
    if (cell >= 0 && cell < 9 && you != CELL_EMPTY && Proto_SeatsOpen() == 0 &&
        Game_Move(s_game, (uint8_t) cell, you)) {
      s_dirty = true;                     /* glavna zanka naj se preriše */
    }
    reply_state(c, you);

  } else if (mg_strcmp(hm->uri, mg_str("/api/new")) == 0) {
    if (you != CELL_EMPTY) { App_NewRound(); s_dirty = true; you = Game_Other(you); }
    reply_state(c, you);

  } else {
    mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", HTML_PAGE);
  }
}
```

![[Pasted image 20260901193954.png]]

Opazi, da je edino, kar /api/move naredi z igro, klic Game_Move() — tisti isti,
ki ga kliče dotik. Nobenega drugega preverjanja. Če ni tvoja poteza, funkcija vrne
false in ni se zgodilo nič.

Stran se osvežuje s poizvedovanjem na 400 ms:

```js
setInterval(() => fetch('/api/state?id=' + id)
  .then(r => r.json()).then(draw), 400);
```

Zakaj poizvedovanje in ne WebSocket? Ker je igra na poteze. 400 ms zamika je neopazno, WebSocket pa bi prinesel rokovanje, SHA-1 in nekaj KB flasha za nič.

Nasprotnikove celice onemogočimo, takoj ko vemo, kdo smo:

```js
c.disabled = !mine || g.seats_open > 0 || v != '' ||
             g.status != 'playing' || g.current_player != g.you;
```

### Kdo je kdo: sedeži

Plošča drži en sedež, brskalnik drugega. Težava je v tem, da je HTTP brez stanja:
vsaka zahteva pride "na novo" in strežnik ne ve, ali je to isti brskalnik kot prej
ali nekdo tretji, ki je stran samo odprl in gleda.

Rešitev je preprosta. Vsak brskalnik si ob prvem obisku ustvari naključen ključ, ga
shrani in ga pošilja z vsako zahtevo:

```js
let id = localStorage.getItem('ttt');
if (!id) { id = Math.random().toString(36).slice(2, 8); localStorage.setItem('ttt', id); }
```

Na plošči imamo tabelo dveh sedežev, od katerih je eden plošče. Prvi ključ, ki
pride, zasede prostega; vsi nadaljnji odjemalci lahko igro samo gledajo:

```c
static char     s_key[2][SEAT_KEY_LEN];   /* 0 = X, 1 = O; "" = prost */
static uint64_t s_seen[2];                /* kdaj se je sedež nazadnje oglasil */

static Cell seat_for(const char *key) {
  if (key == NULL || key[0] == '\0') return CELL_EMPTY;

  for (int i = 0; i < 2; i++)                       /* ga že poznamo? */
    if (s_key[i][0] != '\0' && strcmp(s_key[i], key) == 0) {
      s_seen[i] = mg_millis();
      return seat_mark(i);
    }

  for (int i = 0; i < 2; i++) {                     /* je kaj prostega? */
    if (!seat_is_browsers(i) || s_key[i][0] != '\0') continue;   /* sedež plošče preskoči */
    strncpy(s_key[i], key, SEAT_KEY_LEN - 1);
    s_seen[i] = mg_millis();
    s_dirty = true;
    return seat_mark(i);
  }

  return CELL_EMPTY;                                /* gledalec */
}
```

In ker brskalnik ne pove, kdaj gre stran, sedež po 10 sekundah tišine sprostimo —
sicer zaprt zavihek zablokira igro za vedno:

```c
void Proto_Poll(void) {
  uint64_t now = mg_millis();
  for (int i = 0; i < 2; i++) {
    if (s_key[i][0] == '\0') continue;
    if (now - s_seen[i] < SEAT_TIMEOUT_MS) continue;
    s_key[i][0] = '\0';
    s_dirty = true;
  }
}
```

Ob poizvedovanju na 400 ms je 10 sekund več kot dovolj rezerve.

Ta ista mehanika reši tudi zahtevo, da igralec na plošči **ne sme igrati za
nasprotnika**. Ni treba dodati nobenega preverjanja — dovolj je, da plošči nikoli ne
podamo trenutnega igralca, ampak njen sedež:

```c
/* V lokalni igri zaslon igra oba znaka, v omrežni samo svojega. */
player = (s_mode == MODE_LOCAL) ? s_game.current : Proto_BoardSeat();

if (Game_Move(&s_game, (uint8_t) cell, player)) {
  redraw();
}
```

Če ni na potezi, Game_Move() vrne false. 

## Trije načini

Način izbereš na plošči ob zagonu — z dotikom vrstice ali z modrim gumbom B1:

| Način          | Omrežje               | Kdo igra                              |
| -------------- | --------------------- | ------------------------------------- |
| `LOCAL GAME`   | se ne zažene          | dva človeka pred zaslonom             |
| `DIRECT CABLE` | statični 192.168.2.32 | plošča proti brskalniku, raven kabel  |
| `ROUTER DHCP`  | naslov od routerja    | plošča proti brskalniku, prek omrežja |


V načinu `ROUTER DHCP` je plošča navadna naprava v lokalnem omrežju, torej lahko
nasprotnik igra s telefona na WiFi-ju dokler sta obe napravi v istem omrežju.

Gumb BACK v levem zgornjem kotu pelje nazaj v meni, in to na najbolj neposreden možen
način:

```c
static void go_back_to_menu(void) {
  MG_INFO(("BACK: resetting to the menu"));
  HAL_Delay(50);                                  /* naj UART izprazni */
  NVIC_SystemReset();
}
```

Ja, programska ponastavitev. Ročno podiranje omrežnega sklada je vir napak, ki se
pokažejo šele čez pol ure igranja, meni pa je itak prva stvar po zagonu. Uporabnik
niti ne opazi.

## Glavna zanka

Vse skupaj drži pokonci ena plitka zanka brez blokiranja. V prekinitvah ni ničesar
težkega:

```c
while (1)
{
  int tx, ty;

  if (s_mode != MODE_LOCAL) {
    mg_mgr_poll(&g_mgr, 0);                  /* ARP, IP, TCP, HTTP */

    static uint8_t last_state = 0xFF;        /* naslov, ko DHCP sede */
    if (s_mif.state != last_state) {
      last_state = s_mif.state;
      if (s_mif.state == MG_TCPIP_STATE_READY) ip_to_string();
      redraw();
    }

    Proto_Poll();                            /* sprosti tihe sedeže  */
    if (Proto_TakeDirty()) redraw();         /* poteza iz brskalnika */
  }

  if (Touch_Read(&tx, &ty)) handle_tap(tx, ty);
}
```

Izris se zgodi samo, ko se stanje res spremeni. Ker je redraw() poln zapis 261 KB
slikovnega pomnilnika, je potratno 1000-krat na sekundo.

## BUILD opcije

če se builda v release namesto debug prihrani 30% flash saj optimizira večino kode še posebej mongoose.



