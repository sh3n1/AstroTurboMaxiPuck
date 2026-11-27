# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors

idf_component_register(
    SRCS "main.c" "board.c" "wifi.c" "wifi_setup.c" "wifi_networks.c" "lx200.c" "link.c" "lists.c" "view_state.c" "catalog.c" "sky_draw.c" "sky_pixels.c" "sky_stars.c" "sky_objects.c" "sky_labels.c" "sky_icons.c" "json_read.c" "moving.c" "texts.c" "zoom.c" "view_glide.c" "screen.c" "screenshot.c" "log_history.c" "field_log.c" "menu.c" "menu_lists.c" "align.c" "menu_chip.c" "qr_view.c" "splash.c" "player.c" "images/logo.c" "brightness.c" "fonts/noto_sans_20.c" "fonts/noto_sans_28.c" "fonts/material_symbols_32.c" "fonts/material_symbols_48.c"
    INCLUDE_DIRS "." "fonts"
    PRIV_REQUIRES esp_psram spi_flash esp_driver_spi esp_driver_i2c esp_lcd
                  esp_wifi esp_http_server esp_netif lwip esp_event nvs_flash esp_timer esp_partition libpeer esp_driver_usb_serial_jtag
)
