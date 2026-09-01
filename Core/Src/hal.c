// Copyright (c) 2026 Cesanta Software Limited
// All rights reserved
//
// Trimmed for use inside an existing CubeMX-generated project: SysTick
// handling, syscall stubs, SystemInit, etc. are already provided by
// CubeMX's own stm32h7xx_it.c / system_stm32h7xx.c / syscalls.c / sysmem.c

#include "hal.h"
#include "stm32h7xx_hal.h"   // for HAL_GetTick()

bool hal_timer_expired(volatile uint64_t *t, uint64_t period, uint64_t now) {
  uint64_t diff = now - *t;         // Wrap-safe elapsed time since last expiry
  if (period == 0) return false;    // Avoid division by zero
  if (diff < period) return false;  // Period has not elapsed yet
  *t += (diff / period) * period;   // Preserve cadence, skip missed periods
  return true;
}

// Reuse HAL's own millisecond tick — CubeMX's existing SysTick_Handler()
// (in stm32h7xx_it.c) already increments it via HAL_IncTick().
uint64_t hal_get_tick(void) {
  return HAL_GetTick();
}

extern unsigned char _end[];  // End of data section, start of heap. See linker script
static unsigned char *s_current_heap_end = _end;

size_t hal_ram_used(void) {
  return (size_t) (s_current_heap_end - _end);
}

size_t hal_ram_free(void) {
  unsigned char endofstack;
  return (size_t) (&endofstack - s_current_heap_end);
}