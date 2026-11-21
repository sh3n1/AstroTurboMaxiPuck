# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors

idf_component_register(
    SRCS "main.c" "board.c" "wifi.c" "link.c" "view_state.c" "catalog.c" "sky_draw.c" "sky_objects.c" "json_read.c" "moving.c" "texts.c" "zoom.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_psram spi_flash esp_driver_spi esp_driver_i2c esp_lcd
                  esp_wifi esp_netif esp_event nvs_flash esp_timer esp_partition libpeer
)
