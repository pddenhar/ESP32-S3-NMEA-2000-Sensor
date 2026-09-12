#pragma once

#include "esp_err.h"
#include "n2k_channels.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Which gauges the user last chose, kept in NVS so a panel instrument comes
 * back the way it was left after being switched off with the boat.
 *
 * The selection is stored as channel keys rather than channel ids: ids are
 * indices into a table that will be reordered as channels are added, whereas a
 * key names one channel for good. A PGN would not do either -- one message can
 * feed several channels, so it does not say which of them was chosen (see
 * n2k_channels.h). Anything that no longer maps to a channel is dropped on
 * load.
 *
 * nvs_flash_init() must have been called first.
 */

/**
 * Load the saved selection into @p ids.
 *
 * @param count  set to the number of channels loaded
 * @param max    capacity of @p ids
 * @return ESP_OK when a usable selection was restored; an error when there was
 *         nothing saved or none of it was usable, in which case @p count is 0
 *         and the caller should apply its own default.
 */
esp_err_t ui_prefs_load_channels(n2k_channel_id_t *ids, int *count, int max);

/**
 * Save the selection. Cheap to call on every change: writes are coalesced, so
 * working through the menu costs one flash write rather than one per tap.
 */
void ui_prefs_save_channels(const n2k_channel_id_t *ids, int count);

#ifdef __cplusplus
}
#endif
