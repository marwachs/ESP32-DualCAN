#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* SPI microSD (Adafruit #4682 on SPI3) for boards that define BOARD_HAS_SD.
 * On other targets these are no-ops / unhandled, so the ESP32-C6 build is
 * unchanged. Mount point is /sdcard. Missing cards never fail the boot. */

#ifndef SD_MOUNT_POINT
#define SD_MOUNT_POINT "/sdcard"
#endif

/* Optional auto-mount at boot: logs and continues if no card is present. */
void sd_init(void);

/* Console hook for `sd ...` (and the extra `help` line). Returns true if
 * handled. On boards without SD, always returns false. */
bool sd_console(const char *line, const char *lower);
