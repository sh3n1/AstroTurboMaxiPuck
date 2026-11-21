# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors
#
# Top-level ESP-IDF project file for the AstroTurboMaxiPuck firmware.
# Build with `idf.py build` (target esp32s3, set by sdkconfig.defaults).

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(astroturbomaxipuck)

# libsrtp (sepfy/srtp, needed by libpeer) predates GCC 14, which made mismatched pointer
# types an error. The mismatch is harmless there (const and non-const buffers), so it is
# only a warning for that library.
idf_component_get_property(srtp_lib sepfy__srtp COMPONENT_LIB)
target_compile_options(${srtp_lib} PRIVATE -Wno-error=incompatible-pointer-types)
