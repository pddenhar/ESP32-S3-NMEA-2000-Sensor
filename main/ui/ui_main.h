#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build the application screen on the active display.
 *
 * The panel is a header, a row of gauge tiles, and a drawer for choosing which
 * channels those tiles show. Nothing here knows what any particular gauge looks
 * like or where its data comes from: tiles are built from the channel table in
 * n2k_channels.h through the gauge classes in ui_gauge.h, and the selection is
 * restored from NVS (see ui_prefs.h).
 *
 * Must be called with the LVGL lock held.
 */
void ui_main_create(void);

#ifdef __cplusplus
}
#endif
