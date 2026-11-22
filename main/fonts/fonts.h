// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Noto Sans, for text with letters LVGL's built-in Montserrat lacks: the Greek letters of
// star names (α Tau), and accented Latin. Generated once with lv_font_conv (see README.md
// here); LVGL's Montserrat stays for the symbols on buttons.

#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(noto_sans_20);
LV_FONT_DECLARE(noto_sans_28);

// The menu's icons (icons.h).
LV_FONT_DECLARE(material_symbols_32);
LV_FONT_DECLARE(material_symbols_48); // the main menu's bubbles only
