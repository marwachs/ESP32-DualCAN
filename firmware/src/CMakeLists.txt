FILE(GLOB_RECURSE app_sources
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

# fatfs + sdmmc are only linked on the T-2CAN FD (ESP32-S3) target, which
# has the SPI3 microSD slot. The ESP32-C6 build must not pull them in.
set(extra_requires "")
if(IDF_TARGET STREQUAL "esp32s3")
    list(APPEND extra_requires fatfs sdmmc)
endif()

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_usb_serial_jtag esp_driver_twai esp_driver_gpio esp_driver_spi acan2517fd berry led_strip littlefs esp_partition vfs bt json
             esp_wifi esp_event esp_netif nvs_flash app_update mbedtls tesla-ble esp_http_server
             ${extra_requires}
)
