#pragma once

/*
 * Mongoose build configuration -- STM32H750B-DK, Tic-Tac-Toe over Ethernet.
 *
 * THIS FILE IS THE ONLY PLACE MONGOOSE IS CONFIGURED.
 * It is included from mongoose.h AFTER any -D flags, so anything defined here
 * silently overrides target_compile_definitions() in CMakeLists.txt. Do not
 * set MG_* options in both places.
 */

/* Bare-metal ARM GCC. Left as it was -- this is what currently compiles. */
#define MG_ARCH MG_ARCH_ARMGCC

/* Mongoose's own TCP/IP stack (no lwIP, no BSD sockets). */
#define MG_ENABLE_TCPIP 1

/* main.c provides mg_millis() and mg_random(). */
#define MG_ENABLE_CUSTOM_MILLIS 1
#define MG_ENABLE_CUSTOM_RANDOM 1

/*
 * No TLS.
 *
 * MG_TLS_BUILTIN makes mg_mgr_init() call into the built-in crypto stack,
 * which hangs on this target and costs a large amount of flash. We speak
 * plain TCP on a point-to-point LAN, so none of it is needed.
 */
#define MG_TLS MG_TLS_NONE

/*
 * Mongoose's bundled STM32H driver programs SYSCFG for RMII. This board
 * (MB1381) wires the LAN8740 in MII -- see UM2488 section 6.10 -- so that
 * driver is unusable here. main.c supplies its own HAL_ETH-based driver
 * and assigns it to mif->driver before mg_tcpip_init().
 */
#define MG_ENABLE_DRIVER_STM32H 0
#define MG_ENABLE_TCPIP_DRIVER_INIT 0

/* No filesystem, no HTTP extras. Phase 7 can turn some of this back on. */
#define MG_ENABLE_POSIX_FS 0
#define MG_ENABLE_FILE 0
#define MG_ENABLE_DIRLIST 0
#define MG_ENABLE_SSI 0
#define MG_ENABLE_PACKED_FS 0