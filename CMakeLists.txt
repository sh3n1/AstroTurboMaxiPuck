# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors
#
# Top-level ESP-IDF project file for the AstroTurboMaxiPuck firmware.
# Build with `idf.py build` (target esp32s3, set by sdkconfig.defaults).

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(astroturbomaxipuck)
