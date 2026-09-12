#pragma once

#include "lvgl.h"
#include "n2k_channels.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How the panel talks to a gauge widget, so tiles can be created and destroyed
 * as the user picks channels without ui_main knowing what any of them look
 * like.
 *
 * A widget keeps its own struct in the tile's user data and frees it from an
 * LV_EVENT_DELETE callback, so a tile is disposed of with plain
 * lv_obj_delete() and this interface needs nothing for teardown.
 *
 * Both functions must be called with the LVGL lock held.
 */
typedef struct {
    /**
     * Build the tile inside @p parent, square, @p size pixels on a side.
     * Returns the tile root, or NULL if it could not be created.
     */
    lv_obj_t *(*create)(lv_obj_t *parent, int32_t size, const char *title);

    /**
     * Show @p reading. The widget handles reading->valid being false itself,
     * which is what lets the poll loop stay free of per-channel special cases.
     */
    void (*update)(lv_obj_t *tile, const n2k_reading_t *reading);
} ui_gauge_class_t;

#ifdef __cplusplus
}
#endif
