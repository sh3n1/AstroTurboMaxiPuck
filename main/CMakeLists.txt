# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors

idf_component_register(
    SRCS "main.c" "board.c" "wifi.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_psram spi_flash esp_driver_spi esp_driver_i2c esp_lcd
                  esp_wifi esp_netif esp_event nvs_flash
)
