#pragma once

#include "driver/spi_master.h"

/* LilyGo T-2CAN FD (ESP32-S3-WROOM-1, one TWAI + MCP2518FD).
 *
 * Sources:
 *   pin_config.h in Xinyuan-LilyGO/T-2Can (T_2Can_Fd):
 *     CAN_TX 7, CAN_RX 6, SPI 12/11/13, CS 10, INT 8, INT0 9, INT1 3, BOOT 0
 *   project/T-2Can-Fd_V1.0.pdf text next to the MCP2518FD: crystal "40MHZ±10ppm"
 *   TD501MCAN pinout has no standby/enable pin (RXD, TXD, GND, VCC, CANG, CANL, CANH)
 *
 * Bus map — change BOARD_T2CAN_MCP_BUS to swap which Dorky bus uses the
 * external controller. Bus 0 is Tesla vehicle CAN, bus 1 is chassis CAN.
 * The ESP32-S3 has a single TWAI controller, so the other bus must stay
 * on the MCP2518FD and twai_controller stays 0.
 *
 *   default: bus 0 (vehicle) -> MCP2518FD CAN-A
 *            bus 1 (chassis) -> TWAI CAN-B (TX GPIO7, RX GPIO6)
 *
 * pin_config.h and the V1.0 schematic BOM do not define a user LED.
 * GPIO8 is the MCP2518FD interrupt, so the C6 WS2812 must not be driven.
 */

#define BOARD_NAME              "LilyGo T-2CAN FD (ESP32-S3)"
#define BOARD_STATUS_LED_GPIO   GPIO_NUM_NC
#define BOARD_BOOT_BTN_GPIO     GPIO_NUM_0
#define BOARD_HAS_RGB_LED       0
#define BOARD_RGB_LED_GPIO      GPIO_NUM_NC
#define BOARD_RGB_LED_COUNT     0

/* Adafruit microSD Breakout #4682 on the underside 26-pin field U8.
 * 3.3 V only (U8 pad 1). SPI3 is exclusive to the card; SPI2 stays on
 * the MCP2518FD (SCK=12, MOSI=11, MISO=13, CS=10) and must not be reused.
 *   3V  -> U8 pad 1     GND -> U8 pad 2
 *   CLK -> U8 pad 15    DO/SO (MISO) -> U8 pad 14
 *   CMD/SI (MOSI) -> U8 pad 16    D3/CS -> U8 pad 13
 * Card detect is not wired. D1/DAT2 stay open (SPI mode, not 4-bit SDIO).
 */
/* Default 1. Override with -DBOARD_HAS_SD=0 (or the t2can-fd-no-sd env)
 * to compile out the SPI3 driver, `sd` / `sd log` console commands, the
 * logger task/buffer, and the Berry sd_log_* implementations. A missing
 * card or unsoldered breakout never fails the default t2can-fd build. */
#ifndef BOARD_HAS_SD
#define BOARD_HAS_SD            1
#endif
#define BOARD_SD_SPI_HOST       SPI3_HOST
#define BOARD_SD_SCLK_GPIO      GPIO_NUM_15
#define BOARD_SD_MISO_GPIO      GPIO_NUM_4
#define BOARD_SD_MOSI_GPIO      GPIO_NUM_5
#define BOARD_SD_CS_GPIO        GPIO_NUM_16

/* Which logical bus is the MCP2518FD. 0 = vehicle CAN on CAN-A. */
#define BOARD_T2CAN_MCP_BUS     0

#define BOARD_MCP_OSC_HZ        40000000u

static inline void board_can_config(int bus_id, uint32_t bitrate_kbps, can_bus_config_t *out)
{
    can_bus_config_t c = {
        .bus_id = bus_id,
        .bitrate_kbps = bitrate_kbps,
        .stb_pin = GPIO_NUM_NC,
        .tx_pin = GPIO_NUM_NC,
        .rx_pin = GPIO_NUM_NC,
        .spi_sclk = GPIO_NUM_NC,
        .spi_mosi = GPIO_NUM_NC,
        .spi_miso = GPIO_NUM_NC,
        .spi_cs = GPIO_NUM_NC,
        .int_pin = GPIO_NUM_NC,
        .int0_pin = GPIO_NUM_NC,
        .int1_pin = GPIO_NUM_NC,
        .osc_hz = 0,
        .twai_controller = 0,
    };
    if (bus_id == BOARD_T2CAN_MCP_BUS) {
        c.backend = CAN_BACKEND_MCP2518FD;
        c.spi_sclk = GPIO_NUM_12;
        c.spi_mosi = GPIO_NUM_11;
        c.spi_miso = GPIO_NUM_13;
        c.spi_cs   = GPIO_NUM_10;
        c.int_pin  = GPIO_NUM_8;
        c.int0_pin = GPIO_NUM_9;
        c.int1_pin = GPIO_NUM_3;   /* S3 strapping pin; held as input with pull-up */
        c.osc_hz   = BOARD_MCP_OSC_HZ;
    } else {
        c.backend = CAN_BACKEND_TWAI;
        c.twai_controller = 0;
        c.tx_pin = GPIO_NUM_7;
        c.rx_pin = GPIO_NUM_6;
    }
    *out = c;
}
