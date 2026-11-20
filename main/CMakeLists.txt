# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_psram spi_flash
)
