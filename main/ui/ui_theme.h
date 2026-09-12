#pragma once

#include "lvgl.h"

/**
 * The panel's shared palette.
 *
 * Dark by necessity rather than fashion: this screen sits in a cockpit at
 * night, where a light background destroys night vision. The status colours
 * double as the gauge accent colours so that green, amber and red mean the
 * same thing everywhere on the screen.
 *
 * Colours specific to one widget's meaning -- port and starboard, north, the
 * heading reference -- stay in that widget's source, since they are part of
 * what it is saying rather than part of the chrome.
 */
/**
 * Text: the built-in Montserrat fonts carry ASCII (0x20-0x7F), the degree sign
 * "\xC2\xB0", the bullet "\xE2\x80\xA2", and the FontAwesome LV_SYMBOL_* glyphs
 * -- and nothing else. A character outside that set draws as an empty box, at
 * runtime, with no build warning. En and em dashes, curly quotes and the middle
 * dot are the easy ones to reach for by mistake; use a hyphen or the bullet.
 */

#define UI_COLOR_SCREEN_BG lv_color_hex(0x11141A)
#define UI_COLOR_PANEL_BG  lv_color_hex(0x1B1F25)
#define UI_COLOR_BORDER    lv_color_hex(0x2E353E)
#define UI_COLOR_FG        lv_color_hex(0xE8ECF0)
#define UI_COLOR_DIM       lv_color_hex(0x8A939E)
#define UI_COLOR_OK        lv_color_hex(0x30B050)
#define UI_COLOR_WARN      lv_color_hex(0xE0A030)
#define UI_COLOR_BAD       lv_color_hex(0xE03030)
