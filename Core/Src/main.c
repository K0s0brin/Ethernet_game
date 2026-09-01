/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32H750B-DK -- hardware bring-up, Ethernet, Mongoose,
  *                   and the top-level application state machine.
  *
  razdelitev po datotekah:
  lcd.c    framebuffer primitives and the font
  touch.c  I2C4 and the capacitive touch controller
  game.c   Pravila igre
  ui.c     menu screen, game screen, and their hit tests

  */

#include "main.h"
#include "string.h"
#include "mongoose.h"
#include "hal.h"

#include "lcd.h"
#include "touch.h"
#include "game.h"
#include "ui.h"
#include "game_protocol.h"

/* -------------------------------------------------------------------------- */
/* Ethernet Rx buffer allocation for HAL                                      */
/* -------------------------------------------------------------------------- */
static uint8_t s_eth_rx_buffers[ETH_RX_DESC_CNT][1536]
    __attribute__((section(".EthBufSection")));
static uint32_t s_eth_rx_buf_idx;

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
  if (buff == NULL) return;
  *buff = s_eth_rx_buffers[s_eth_rx_buf_idx++];
  if (s_eth_rx_buf_idx >= ETH_RX_DESC_CNT) {
    s_eth_rx_buf_idx = 0;
  }
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
  /* RxBuffLen (1536) always fits one full standard Ethernet frame, so there
   * is no scatter-gather chain to build -- hand the single buffer through as
   * a one-element list. HAL computes RxDataLength itself. */
  (void) Length;
  *pStart = buff;
  *pEnd   = buff;
}

/* -------------------------------------------------------------------------- */
/* Mongoose HAL hooks                                                         */
/* -------------------------------------------------------------------------- */

uint64_t mg_millis(void) {
  return hal_get_tick();
}

/*
 * Random source for Mongoose.
 *
 * hal.h's hal_rng_read() spins forever on RNG_SR DRDY. If the RNG is not
 * producing, that spin is silent and permanent -- and mg_tcpip_init() calls
 * mg_random() before it calls the driver's init(), so the board would die
 * with no output at all. Bounded attempt, then xorshift32. Not cryptographic,
 * which is fine for ephemeral ports and TCP sequence numbers on a LAN.
 */
static uint32_t s_rng_state = 0x2545F491u;

static uint32_t rng32(void) {
  if (RNG->CR & RNG_CR_RNGEN) {
    uint32_t tries = 10000u;
    while ((RNG->SR & RNG_SR_DRDY) == 0u && tries > 0u) tries--;
    if (RNG->SR & RNG_SR_DRDY) return RNG->DR;
  }
  s_rng_state ^= s_rng_state << 13;
  s_rng_state ^= s_rng_state >> 17;
  s_rng_state ^= s_rng_state << 5;
  return s_rng_state;
}

bool mg_random(void *buf, size_t len) {
  for (size_t n = 0; n < len; n += sizeof(uint32_t)) {
    uint32_t r = rng32();
    memcpy((char *) buf + n, &r, n + sizeof(r) > len ? len - n : sizeof(r));
  }
  return true;
}

static void log_fn(char ch, void *param) {
  hal_uart_write_buf((USART_TypeDef *) param, &ch, 1);
}

/* -------------------------------------------------------------------------- */
/* Application state                                                          */
/* -------------------------------------------------------------------------- */

static NetMode s_mode = MODE_DIRECT;
static Game    s_game;
static char    s_ipstr[20];

/* -------------------------------------------------------------------------- */
/* Mongoose Ethernet configuration                                            */
/* -------------------------------------------------------------------------- */

/* Overwritten at boot by mac_from_uid(). */
static uint8_t s_mac[6] = {0x02, 0x00, 0x01, 0x02, 0x03, 0x04};

/*
 * Derive a stable, unique MAC from the STM32's 96-bit unique device ID
 * (RM0433: UID base 0x1FF1E800). Two boards on the same router with the same
 * hard-coded MAC would collide and both fall off the network. Byte 0 = 0x02
 * marks it locally administered, the range reserved for addresses not
 * assigned by the IEEE.
 */
static void mac_from_uid(void) {
  const uint32_t *uid = (const uint32_t *) 0x1FF1E800;
  s_mac[0] = 0x02;
  s_mac[1] = (uint8_t) (uid[0] & 0xFF);
  s_mac[2] = (uint8_t) ((uid[0] >> 10) & 0xFF);
  s_mac[3] = (uint8_t) ((uid[0] >> 19) & 0xFF);
  s_mac[4] = (uint8_t) (uid[1] & 0xFF);
  s_mac[5] = (uint8_t) (uid[2] & 0xFF);
}

struct mg_mgr g_mgr;

/* Must NOT live in .EthBufSection: that section is sized for the Ethernet DMA
 * buffers, while mg_tcpip_if is a plain software struct. */
static struct mg_tcpip_if s_mif;

/* -------------------------------------------------------------------------- */
/* Mongoose event handler for the application TCP listener                    */
/* -------------------------------------------------------------------------- */
static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_ACCEPT) {
    MG_INFO(("client connected"));
    mg_send(c, "HELLO FROM STM32\n", 17);
  } else if (ev == MG_EV_READ) {
    MG_INFO(("got %u bytes", (unsigned) c->recv.len));
    c->recv.len = 0;
  } else if (ev == MG_EV_CLOSE) {
    MG_INFO(("client disconnected"));
  }
  (void) ev_data;
}

/* -------------------------------------------------------------------------- */
/* Ethernet DMA descriptors                                                   */
/* -------------------------------------------------------------------------- */

ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT]
    __attribute__((section(".RxDescripSection")));

ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT]
    __attribute__((section(".TxDescripSection")));

ETH_TxPacketConfig TxConfig;

/* -------------------------------------------------------------------------- */
/* Peripherals                                                                */
/* -------------------------------------------------------------------------- */

ETH_HandleTypeDef   heth;
LTDC_HandleTypeDef  hltdc;
SDRAM_HandleTypeDef hsdram1;

/* -------------------------------------------------------------------------- */
/* Function prototypes                                                        */
/* -------------------------------------------------------------------------- */

void SystemClock_Config(void);

static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_ETH_Init(void);
static void MX_FMC_Init(void);
static void MX_LTDC_Init(void);
static void MX_UART3_Raw_Init(void);

/* -------------------------------------------------------------------------- */
/* PHY (LAN8740) bring-up over MDIO                                           */
/* -------------------------------------------------------------------------- */

#define ETH_PHY_BCR   0x00u
#define ETH_PHY_BSR   0x01u
#define ETH_PHY_ID1   0x02u
#define ETH_PHY_ID2   0x03u
#define ETH_PHY_SCSR  0x1Fu   /* LAN874x special control/status */

#define ETH_PHY_BCR_AN_ENABLE   (1u << 12)
#define ETH_PHY_BCR_AN_RESTART  (1u << 9)
#define ETH_PHY_BSR_AN_DONE     (1u << 5)
#define ETH_PHY_BSR_LINK_UP     (1u << 2)

static uint32_t s_phy_addr = 0xFFFFFFFFu;

static bool ETH_PhyFind(void)
{
  for (uint32_t a = 0; a < 32u; a++) {
    uint32_t id1 = 0, id2 = 0;
    if (HAL_ETH_ReadPHYRegister(&heth, a, ETH_PHY_ID1, &id1) != HAL_OK) continue;
    if (HAL_ETH_ReadPHYRegister(&heth, a, ETH_PHY_ID2, &id2) != HAL_OK) continue;
    if (id1 == 0x0000u || id1 == 0xFFFFu) continue;
    s_phy_addr = a;
    MG_INFO(("PHY at addr %u, ID %04x:%04x",
             (unsigned) a, (unsigned) id1, (unsigned) id2));
    return true;
  }
  MG_ERROR(("PHY NOT FOUND on MDIO"));
  return false;
}

static bool ETH_PhyBringUp(void)
{
  uint32_t bsr = 0, scsr = 0, start;
  ETH_MACConfigTypeDef mac = {0};

  if (!ETH_PhyFind()) return false;

  HAL_ETH_WritePHYRegister(&heth, s_phy_addr, ETH_PHY_BCR,
                           ETH_PHY_BCR_AN_ENABLE | ETH_PHY_BCR_AN_RESTART);

  /* BSR link bit is latching-low: read it twice. */
  start = HAL_GetTick();
  do {
    HAL_ETH_ReadPHYRegister(&heth, s_phy_addr, ETH_PHY_BSR, &bsr);
    HAL_ETH_ReadPHYRegister(&heth, s_phy_addr, ETH_PHY_BSR, &bsr);
    if ((bsr & ETH_PHY_BSR_LINK_UP) && (bsr & ETH_PHY_BSR_AN_DONE)) break;
  } while ((HAL_GetTick() - start) < 5000u);

  if (!(bsr & ETH_PHY_BSR_LINK_UP)) {
    MG_ERROR(("NO LINK (BSR=%04x)", (unsigned) bsr));
    return false;
  }

  HAL_ETH_ReadPHYRegister(&heth, s_phy_addr, ETH_PHY_SCSR, &scsr);

  /* HAL_ETH_Init() leaves the MAC at 10 Mbit half duplex. Without this the
   * MAC samples at the wrong rate and never assembles a frame. */
  HAL_ETH_GetMACConfig(&heth, &mac);
  switch ((scsr >> 2) & 0x07u) {
    case 1: mac.Speed = ETH_SPEED_10M;  mac.DuplexMode = ETH_HALFDUPLEX_MODE; break;
    case 5: mac.Speed = ETH_SPEED_10M;  mac.DuplexMode = ETH_FULLDUPLEX_MODE; break;
    case 2: mac.Speed = ETH_SPEED_100M; mac.DuplexMode = ETH_HALFDUPLEX_MODE; break;
    case 6: mac.Speed = ETH_SPEED_100M; mac.DuplexMode = ETH_FULLDUPLEX_MODE; break;
    default: mac.Speed = ETH_SPEED_100M; mac.DuplexMode = ETH_FULLDUPLEX_MODE; break;
  }
  HAL_ETH_SetMACConfig(&heth, &mac);

  MG_INFO(("LINK UP: %s %s (BSR=%04x SCSR=%04x)",
           mac.Speed == ETH_SPEED_100M ? "100M" : "10M",
           mac.DuplexMode == ETH_FULLDUPLEX_MODE ? "FULL" : "HALF",
           (unsigned) bsr, (unsigned) scsr));
  return true;
}


/* Ethernet MII pins + clocks                                                                                                                       
MB1381 wires the LAN8740 in MII, UM2488 section 6.10, and the  
MII_* pin labels in Ethernet_game.ioc. This replaces hal_ethernet_init()   
from hal.h, which is written for the H747I-DISCO (RMII) mongoose imel narejeno samo za board H747I-DISCO
*/

static void ETH_MII_HwInit(void)
{
  static const uint16_t pins[] = {
      PIN('A',  1),   /* MII_RX_CLK */
      PIN('A',  2),   /* MII_MDIO   */
      PIN('A',  7),   /* MII_RX_DV  */
      PIN('B',  0),   /* MII_RXD2   */
      PIN('B',  1),   /* MII_RXD3   */
      PIN('C',  1),   /* MII_MDC    */
      PIN('C',  2),   /* MII_TXD2   */
      PIN('C',  3),   /* MII_TX_CLK */
      PIN('C',  4),   /* MII_RXD0   */
      PIN('C',  5),   /* MII_RXD1   */
      PIN('E',  2),   /* MII_TXD3   */
      PIN('G', 11),   /* MII_TX_EN  */
      PIN('G', 12),   /* MII_TXD1   */
      PIN('G', 13),   /* MII_TXD0   */
      PIN('I', 10),   /* MII_RX_ER  */
  };

  __HAL_RCC_SYSCFG_CLK_ENABLE();

  for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
    hal_gpio_init(pins[i], HAL_GPIO_MODE_AF, HAL_GPIO_OTYPE_PUSH_PULL,
                  HAL_GPIO_SPEED_INSANE, HAL_GPIO_PULL_NONE, 11);  /* AF11 */
  }

  /* MII = EPIS_SEL 000, written BEFORE the ETH clocks are enabled. */
  SYSCFG->PMCR &= ~(7UL << 21);

  RCC->AHB1ENR |= BIT(15) | BIT(16) | BIT(17);  /* ETH1MAC / ETH1TX / ETH1RX */
  (void) RCC->AHB1ENR;
}

/* Mongoose's own TX buffer lives in DTCM, which the Ethernet DMA cannot read,
 * so every outgoing frame is bounced through D2 RAM. */
static uint8_t s_tx_bounce[1600] __attribute__((section(".EthBufSection")));
static bool s_link_up;

static bool drv_init(struct mg_tcpip_if *ifp) {
  (void) ifp;
  if (HAL_ETH_Start(&heth) != HAL_OK) {
    MG_ERROR(("HAL_ETH_Start failed"));
    return false;
  }
  s_link_up = true;
  MG_INFO(("ETH started"));
  return true;
}

static size_t drv_tx(const void *buf, size_t len, struct mg_tcpip_if *ifp) {
  ETH_BufferTypeDef txbuf;
  ETH_TxPacketConfig txcfg;
  (void) ifp;
  if (buf == NULL || len == 0 || len > sizeof(s_tx_bounce)) return 0;
  memcpy(s_tx_bounce, buf, len);

  memset(&txbuf, 0, sizeof(txbuf));
  memset(&txcfg, 0, sizeof(txcfg));
  txbuf.buffer     = s_tx_bounce;
  txbuf.len        = (uint32_t) len;
  txbuf.next       = NULL;
  txcfg.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
  txcfg.Length     = (uint32_t) len;
  txcfg.TxBuffer   = &txbuf;
  txcfg.CRCPadCtrl = ETH_CRC_PAD_INSERT;

  if (HAL_ETH_Transmit(&heth, &txcfg, 100) != HAL_OK) {
    MG_ERROR(("TX failed, len=%u", (unsigned) len));
    return 0;
  }
  return len;
}

static size_t drv_rx(void *buf, size_t len, struct mg_tcpip_if *ifp) {
  void *pbuf = NULL;
  size_t n;
  (void) ifp;
  if (HAL_ETH_ReadData(&heth, &pbuf) != HAL_OK || pbuf == NULL) return 0;
  n = (size_t) heth.RxDescList.RxDataLength;
  if (n > len) n = len;
  memcpy(buf, pbuf, n);
  return n;
}

static bool drv_poll(struct mg_tcpip_if *ifp, bool s1) {
  uint32_t bsr = 0;
  (void) ifp;
  if (s1 && s_phy_addr != 0xFFFFFFFFu) {          /* once per second */
    HAL_ETH_ReadPHYRegister(&heth, s_phy_addr, ETH_PHY_BSR, &bsr);
    HAL_ETH_ReadPHYRegister(&heth, s_phy_addr, ETH_PHY_BSR, &bsr);
    s_link_up = (bsr & ETH_PHY_BSR_LINK_UP) != 0;
  }
  return s_link_up;
}

static struct mg_tcpip_driver s_drv = {
    .init = drv_init,
    .tx   = drv_tx,
    .rx   = drv_rx,
    .poll = drv_poll,
};

static void netif_init(struct mg_mgr *mgr) {
  struct mg_tcpip_if *mif = &s_mif;
  memset(mif, 0, sizeof(*mif));

  /* sizeof(mif->mac) is 8 in Mongoose 7.22 (mg_l2addr-sized, to cover
   * non-Ethernet L2 types). Copy 6 and leave mac[6..7] zeroed. */
  memcpy(mif->mac, s_mac, sizeof(s_mac));

  if (s_mode == MODE_ROUTER) {
    mif->ip = mif->mask = mif->gw = 0;            /* enables the DHCP client */
  } else {
    mif->ip   = mg_htonl(MG_U32(192, 168, 2, 32));
    mif->mask = mg_htonl(MG_U32(255, 255, 255, 0));
    mif->gw   = mg_htonl(MG_U32(192, 168, 2, 1));
  }

  mif->driver      = &s_drv;
  mif->driver_data = NULL;

  /* Left at 0, mg_tcpip_init() would size this to a single frame because our
   * driver has an rx() callback. */
  mif->recv_queue.size = 8192;

  mg_tcpip_init(mgr, mif);

  MG_INFO(("NET: started, framesize=%u mtu=%u queue=%u",
           (unsigned) mif->framesize, (unsigned) mif->mtu,
           (unsigned) mif->recv_queue.size));

  if (mif->tx.buf == NULL) MG_ERROR(("NET: tx buffer alloc failed (heap)"));
  if (mif->framesize > sizeof(s_tx_bounce)) {
    MG_ERROR(("NET: framesize %u > tx bounce %u",
              (unsigned) mif->framesize, (unsigned) sizeof(s_tx_bounce)));
  }
}


/* CELL_EMPTY in a local game: the screen plays both sides. */
static Cell board_seat(void) {
  return (s_mode == MODE_LOCAL) ? CELL_EMPTY : Proto_BoardSeat();
}

/* A network game has no opponent until a browser has talked to us. */
static bool waiting_for_opponent(void) {
  return s_mode != MODE_LOCAL && !Proto_RemoteJoined();
}

static void redraw(void) {
  UI_DrawGame(&s_game, s_mode, s_ipstr[0] ? s_ipstr : NULL,
              board_seat(), waiting_for_opponent());
}

/*
 * Next round.
 *
 * Local game: the two people at the screen alternate who opens.
 * Network game: X always opens, and the board and the browser swap seats --
 * so whoever went second last round opens the next one.
 *
 * The browser's NEW ROUND button calls this through App_NewRound(), so both
 * sides go through exactly the same code.
 */
void App_NewRound(void) {
  if (s_mode == MODE_LOCAL) {
    Game_NewRound(&s_game, Game_Other(s_game.starter));
  } else {
    Proto_SwapSeats();
    Game_NewRound(&s_game, CELL_X);
  }
  MG_INFO(("GAME: round %u, %s opens, board plays %s",
           (unsigned) s_game.round, Game_CellName(s_game.starter),
           s_mode == MODE_LOCAL ? "both" : Game_CellName(Proto_BoardSeat())));
}

static void ip_to_string(void) {
  uint32_t ip = mg_ntohl(s_mif.ip);
  mg_snprintf(s_ipstr, sizeof(s_ipstr), "%u.%u.%u.%u",
              (unsigned) ((ip >> 24) & 255), (unsigned) ((ip >> 16) & 255),
              (unsigned) ((ip >> 8) & 255),  (unsigned) (ip & 255));
}

// nazaj menu samo reseta board 
static void go_back_to_menu(void) {
  MG_INFO(("BACK: resetting to the menu"));
  HAL_Delay(1);                                  // popravi serial monitor izpis
  NVIC_SystemReset();
}

static void handle_tap(int x, int y) {
  Cell player;
  int cell;

  if (UI_BackAt(x, y)) {
    go_back_to_menu();
    return;
  }

  if (s_game.status != GS_PLAYING) {
    App_NewRound();                               
    redraw();
    return;
  }

  cell = UI_CellAt(x, y);
  if (cell < 0) return;

  player = (s_mode == MODE_LOCAL) ? s_game.current : Proto_BoardSeat();

  if (Game_Move(&s_game, (uint8_t) cell, player)) {
    MG_INFO(("MOVE: player=%s cell=%d status=%s (board)",
             Game_CellName(s_game.board[cell]), cell,
             Game_StatusName(s_game.status)));
    redraw();
  }
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void)
{
  MPU_Config();
  HAL_Init();
  SystemClock_Config();
  hal_rng_init();

  MX_UART3_Raw_Init();
  mg_log_set_fn(log_fn, USART3);
  mg_log_set(MG_LL_DEBUG);
  MG_INFO(("BOOT, HCLK=%u Hz", (unsigned) HAL_RCC_GetHCLKFreq()));

  MX_GPIO_Init();
  MG_INFO(("GPIO OK"));

  Touch_Init();

  MX_FMC_Init();          /* SDRAM is not used; the framebuffer is in AXI SRAM */
  MG_INFO(("FMC OK"));

  LCD_Fill(COL_BG);    
  MX_LTDC_Init();
  MG_INFO(("LTDC OK"));

  mac_from_uid();
  MG_INFO(("MAC %02x:%02x:%02x:%02x:%02x:%02x",
           s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]));

  ETH_MII_HwInit();
  MX_ETH_Init();
  MG_INFO(("ETH INIT OK"));

  if (!ETH_PhyBringUp()) {
    MG_ERROR(("PHY bring-up failed - RX will not work"));
  }

  // network ne starta dokler ne izberemo načina v to stanje tudi vrne board ko damo back

  s_mode = UI_SelectMode();
  MG_INFO(("MODE: %s", MODE_NAME[s_mode]));

  if (s_mode != MODE_LOCAL) {
    mg_mgr_init(&g_mgr);
    netif_init(&g_mgr);

    if (mg_listen(&g_mgr, "tcp://0.0.0.0:12345", ev_handler, NULL) != NULL) {
      MG_INFO(("Listening on TCP/12345"));
    } else {
      MG_ERROR(("Failed to create listener"));
    }
  }

  Game_Init(&s_game);

  if (s_mode != MODE_LOCAL) Proto_Start(&g_mgr, &s_game, CELL_X);

  redraw();

  /* Main loop                                                              */

  while (1)
  {
    int tx, ty;

    if (s_mode != MODE_LOCAL) {
      mg_mgr_poll(&g_mgr, 0); // ARP, IP, TCP, HTTP

      {
        static uint8_t last_state = 0xFF; 
        if (s_mif.state != last_state) {
          last_state = s_mif.state;
          if (s_mif.state == MG_TCPIP_STATE_READY) {
            ip_to_string();
            MG_INFO(("NET READY: %s", s_ipstr));
          }
          redraw();
        }
      }

      // preveri če je karkoli prišlo iz drugega klienta
      if (Proto_TakeDirty()) redraw();
    }

    if (Touch_Read(&tx, &ty)) {
      handle_tap(tx, ty);
    }
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
  {
  }

  RCC_OscInitStruct.OscillatorType =
      RCC_OSCILLATORTYPE_HSI |
      RCC_OSCILLATORTYPE_LSI |
      RCC_OSCILLATORTYPE_HSE;

  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;

  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 22;
  RCC_OscInitStruct.PLL.PLLN = 169;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_0;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType =
      RCC_CLOCKTYPE_HCLK |
      RCC_CLOCKTYPE_SYSCLK |
      RCC_CLOCKTYPE_PCLK1 |
      RCC_CLOCKTYPE_PCLK2 |
      RCC_CLOCKTYPE_D3PCLK1 |
      RCC_CLOCKTYPE_D1PCLK1;


  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1)
      != HAL_OK)
  {
    Error_Handler();
  }
}

// Ethernet

static void MX_ETH_Init(void)
{

  heth.Instance = ETH;


  heth.Init.MACAddr = &s_mac[0];

  /*
  STM32H750B-DK / MB1381: LAN8740 je v MII
  UM2488 section 6.10 in MII_* pin labels v Ethernet_game.ioc.
   */
  heth.Init.MediaInterface = HAL_ETH_MII_MODE;

  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;

  heth.Init.RxBuffLen = 1536;

  if (HAL_ETH_Init(&heth) != HAL_OK)
  {
    Error_Handler();
  }

  memset(&TxConfig, 0, sizeof(ETH_TxPacketConfig));

  TxConfig.Attributes =
      ETH_TX_PACKETS_FEATURES_CSUM |
      ETH_TX_PACKETS_FEATURES_CRCPAD;

  TxConfig.ChecksumCtrl =
      ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;

  TxConfig.CRCPadCtrl =
      ETH_CRC_PAD_INSERT;
}

static void MX_LTDC_Init(void)
{
  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  hltdc.Instance = LTDC;

  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;

  /* RK043FN48H: HSYNC 41, HBP 13, HFP 32, VSYNC 10, VBP 2, VFP 2 */
  hltdc.Init.HorizontalSync     = 40;    /* HSYNC - 1              */
  hltdc.Init.VerticalSync       = 9;     /* VSYNC - 1              */
  hltdc.Init.AccumulatedHBP     = 53;    /* HSYNC + HBP - 1        */
  hltdc.Init.AccumulatedVBP     = 11;    /* VSYNC + VBP - 1        */
  hltdc.Init.AccumulatedActiveW = 533;   /* + width                */
  hltdc.Init.AccumulatedActiveH = 283;   /* + height               */
  hltdc.Init.TotalWidth         = 565;   /* + HFP                  */
  hltdc.Init.TotalHeigh         = 285;   /* + VFP                  */

  hltdc.Init.Backcolor.Blue  = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red   = 0;

  if (HAL_LTDC_Init(&hltdc) != HAL_OK) Error_Handler();

  pLayerCfg.WindowX0        = 0;
  pLayerCfg.WindowX1        = LCD_W;
  pLayerCfg.WindowY0        = 0;
  pLayerCfg.WindowY1        = LCD_H;
  pLayerCfg.PixelFormat     = LTDC_PIXEL_FORMAT_RGB565;
  pLayerCfg.Alpha           = 255;
  pLayerCfg.Alpha0          = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  pLayerCfg.FBStartAdress   = (uint32_t) LCD_FB;
  pLayerCfg.ImageWidth      = LCD_W;
  pLayerCfg.ImageHeight     = LCD_H;

  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK) Error_Handler();
}

/* -------------------------------------------------------------------------- */
/* External SDRAM                                                             */
/* -------------------------------------------------------------------------- */

static void MX_FMC_Init(void)
{
  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  hsdram1.Instance = FMC_SDRAM_DEVICE;

  hsdram1.Init.SDBank = FMC_SDRAM_BANK2;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_1;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;


  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_DISABLE;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_DISABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;

  SdramTiming.LoadToActiveDelay = 16;
  SdramTiming.ExitSelfRefreshDelay = 16;
  SdramTiming.SelfRefreshTime = 16;
  SdramTiming.RowCycleDelay = 16;
  SdramTiming.WriteRecoveryTime = 16;
  SdramTiming.RPDelay = 16;
  SdramTiming.RCDDelay = 16;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler();
  }
}

/* -------------------------------------------------------------------------- */
/* UART3 - raw register driver (hal.h), used for Mongoose logging            */
/* (wired to onboard ST-LINK VCP, per Ethernet_game.ioc)                     */
/* -------------------------------------------------------------------------- */

/*
 * TX/RX pins and AF numbers confirmed from Ethernet_game.ioc:
 *   USART3_TX -> PB10 (AF7)  -- labeled VCP_TX, wired to onboard ST-LINK
 *   USART3_RX -> PB11 (AF7)  -- labeled VCP_RX, wired to onboard ST-LINK
 *
 * NOTE: USART1 (PB14/PB7) is NOT connected to the onboard ST-LINK VCP
 * on this board -- nothing will show up on COM3 if you log through it.
 * USART3 is the one that reaches your PC's serial monitor.
 */
static void MX_UART3_Raw_Init(void)
{
  hal_uart_init(USART3,
                PIN('B', 10), 7,   // TX: PB10, AF7
                PIN('B', 11), 7,   // RX: PB11, AF7
                115200);
}

/* -------------------------------------------------------------------------- */
/* GPIO                                                                       */
/* -------------------------------------------------------------------------- */

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /*
   * Keep the GPIO clocks from your original board configuration.
   *
   * The actual alternate-function configuration for Ethernet,
   * FMC, LTDC and UART is performed by their HAL MSP functions.
   */
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOK_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOJ_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();

  /*
   * LEDs
   */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET); // nerabijo vec sploh so bile za debug
  HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);

  HAL_GPIO_WritePin(LCD_DISPD7_GPIO_Port, LCD_DISPD7_Pin, GPIO_PIN_SET);
  GPIO_InitStruct.Pin   = LCD_DISPD7_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_DISPD7_GPIO_Port, &GPIO_InitStruct);

  /*
   * Board button.
   */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;

  HAL_GPIO_Init(
      B1_GPIO_Port,
      &GPIO_InitStruct);

  /*
   * LD2
   */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  HAL_GPIO_Init(
      LD2_GPIO_Port,
      &GPIO_InitStruct);

  /*
   * LCD interrupt / touch interrupt line.
   *
   * IMPORTANT:
   * This is only the GPIO interrupt configuration that existed
   * in your original main.c.
   *
   * The actual touch-controller driver is NOT present in this main.c.
   */
  GPIO_InitStruct.Pin = LCD_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;

  HAL_GPIO_Init(
      LCD_INT_GPIO_Port,
      &GPIO_InitStruct);

  /*
   * LCD backlight (PK0). Left as an input on purpose: the panel is already
   * lit in this configuration, so this is not touched. If the backlight ever
   * needs software control, make it an output here and check the polarity
   * against the board schematic first.
   */
  GPIO_InitStruct.Pin = LCD_BL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;

  HAL_GPIO_Init(
      LCD_BL_GPIO_Port,
      &GPIO_InitStruct);

  /*
   * LCD reset.
   */
  GPIO_InitStruct.Pin = LCD_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  HAL_GPIO_Init(
      LCD_RST_GPIO_Port,
      &GPIO_InitStruct);

  /*
   * LD1
   */
  GPIO_InitStruct.Pin = LD1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  HAL_GPIO_Init(
      LD1_GPIO_Port,
      &GPIO_InitStruct);
}


void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  HAL_MPU_Disable();

  MPU_InitStruct.Enable            = MPU_REGION_ENABLE;
  MPU_InitStruct.Number            = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress       = 0x30000000;
  MPU_InitStruct.Size              = MPU_REGION_SIZE_1MB; /* covers 288K, base is 1MB-aligned */
  MPU_InitStruct.SubRegionDisable  = 0x0;
  MPU_InitStruct.TypeExtField      = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission  = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec       = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable       = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable       = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable      = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}


void Error_Handler(void)
{
  __disable_irq();

  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT

void assert_failed(uint8_t *file, uint32_t line)
{
  /*
   * Add debugging output here if needed.
   */
}

#endif