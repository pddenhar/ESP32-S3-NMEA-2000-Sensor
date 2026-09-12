#pragma once

#include "lvgl.h"
#include "n2k_channels.h"
#include "ui_gauge.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Slide-out drawer for choosing which channels are on screen.
 *
 * One row per channel that has a gauge to draw it, each showing the channel
 * name, its PGN, and whether data is arriving right now -- so a gauge can be
 * chosen before its sender is powered up, and a silent one can be told apart
 * from a missing one.
 *
 * All functions must be called with the LVGL lock held.
 */

/** Called when the selection changes, with the channels to show, in tile order. */
typedef void (*ui_menu_apply_cb_t)(const n2k_channel_id_t *ids, int count);

/**
 * Build the drawer (hidden) over @p parent.
 *
 * @param gauge_classes  per-channel gauge table, indexed by channel id; a NULL
 *                       entry means the channel has no widget yet and is left
 *                       out of the list
 * @param max_selected   how many channels may be shown at once
 * @param on_apply       invoked on every change to the selection
 */
void ui_menu_create(lv_obj_t *parent,
                    const ui_gauge_class_t *const *gauge_classes,
                    int max_selected,
                    ui_menu_apply_cb_t on_apply);

/** Slide the drawer in. */
void ui_menu_open(void);

/** Slide the drawer out. */
void ui_menu_close(void);

/** Point the checkboxes at @p ids without invoking the apply callback. */
void ui_menu_set_selection(const n2k_channel_id_t *ids, int count);

#ifdef __cplusplus
}
#endif
