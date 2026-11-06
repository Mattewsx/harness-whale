#pragma once
/* Offline deep sleep for whale mode. Reuses the BSP shutdown path proven by
 * the Low Power demo (button hand-back, ES8311/I2S/I2C release, ST7789 sleep). */
#include <stdbool.h>
#include <stdint.h>
#include "whale_clock.h"

typedef enum {
    WHALE_BOOT_POWER, /* power-on, reset button or flashing */
    WHALE_BOOT_KEY,   /* key wake from offline sleep */
    WHALE_BOOT_TIMER, /* periodic wake to look for the computer */
} whale_boot_t;

typedef struct {
    whale_boot_t boot;
    uint32_t cycles; /* consecutive timer wakes without a computer */
    uint32_t sleeps; /* offline sleeps since power-on */
} whale_sleep_info_t;

whale_sleep_info_t whale_sleep_boot_info(void);
const char *whale_sleep_boot_name(whale_boot_t boot);
/* Runs on the peripheral task, which owns the shared I2C bus. Returns false
 * when sleep was refused and the app is unchanged (key held, BLE busy);
 * otherwise it does not return. */
bool whale_sleep_enter(uint32_t timer_ms, uint32_t cycles);
/* Wall clock kept in RTC memory so it survives offline deep sleep. Any other
 * boot clears it (RTC seconds restart at power-on); the next bridge sync
 * sets it again. */
whale_clock_t *whale_sleep_clock(void);
