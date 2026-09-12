#include "ui_prefs.h"

#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "nvs.h"

static const char *TAG = "ui_prefs";

#define PREFS_NAMESPACE "ui"
#define PREFS_KEY_TILES "tiles"

/* Headroom over the number of tiles the panel shows today, so making room for
 * more does not invalidate what is already stored. */
#define PREFS_MAX_CHANNELS 4

/* Bumped if the layout of the blob ever changes; an older or newer record is
 * discarded rather than misread. Version 2 stores channel keys: version 1
 * stored PGNs, which stopped identifying a channel once one message (127489)
 * came to feed several. */
#define PREFS_VERSION 2

typedef struct {
    uint8_t version;
    uint8_t count;
    char keys[PREFS_MAX_CHANNELS][N2K_CHANNEL_KEY_MAX];
} prefs_blob_t;

/* Flash writes are coalesced: the menu can fire several changes in a second as
 * the user works through the checkboxes, and each one would otherwise be an
 * erase/write cycle. Runs in the LVGL task, so the commit briefly blocks
 * drawing -- acceptable for something that happens when a human taps a box. */
#define PREFS_WRITE_DELAY_MS 2000

static lv_timer_t *s_write_timer;
static prefs_blob_t s_pending;

static void write_now(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PREFS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot open NVS for writing: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs, PREFS_KEY_TILES, &s_pending, sizeof(s_pending));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot save gauge selection: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Saved %u gauge(s)", (unsigned)s_pending.count);
    }
}

static void write_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    /* LVGL deletes a one-shot timer as soon as its callback returns, so the
     * handle has to be dropped here or the next save would re-arm freed
     * memory. */
    s_write_timer = NULL;
    write_now();
}

esp_err_t ui_prefs_load_channels(n2k_channel_id_t *ids, int *count, int max)
{
    if (ids == NULL || count == NULL || max <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = 0;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PREFS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        /* Nothing saved yet is the normal first-boot path, not a fault. */
        return err;
    }

    prefs_blob_t blob;
    size_t length = sizeof(blob);
    err = nvs_get_blob(nvs, PREFS_KEY_TILES, &blob, &length);
    nvs_close(nvs);

    if (err != ESP_OK) {
        return err;
    }
    if (length != sizeof(blob) || blob.version != PREFS_VERSION ||
        blob.count > PREFS_MAX_CHANNELS) {
        ESP_LOGW(TAG, "Discarding unrecognised saved selection");
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < blob.count && *count < max; i++) {
        /* Guarantee termination before the string is used: the record came off
         * flash and nothing guarantees what is in it. */
        blob.keys[i][N2K_CHANNEL_KEY_MAX - 1] = '\0';

        /* A channel this firmware no longer has is dropped rather than
         * poisoning the whole selection. */
        const int id = n2k_channels_find_by_key(blob.keys[i]);
        if (id < 0) {
            ESP_LOGW(TAG, "Saved channel \"%s\" is not known, ignoring", blob.keys[i]);
            continue;
        }
        ids[(*count)++] = (n2k_channel_id_t)id;
    }

    return (*count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void ui_prefs_save_channels(const n2k_channel_id_t *ids, int count)
{
    if (ids == NULL || count < 0) {
        return;
    }
    if (count > PREFS_MAX_CHANNELS) {
        count = PREFS_MAX_CHANNELS;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.version = PREFS_VERSION;
    for (int i = 0; i < count; i++) {
        const n2k_channel_desc_t *desc = n2k_channels_desc(ids[i]);
        if (desc == NULL || desc->key == NULL) {
            continue;
        }
        /* strncpy into a zeroed buffer: the key is a compile-time constant
         * short enough to fit, and a truncated one simply fails to match on
         * load rather than corrupting anything. */
        strncpy(s_pending.keys[s_pending.count], desc->key, N2K_CHANNEL_KEY_MAX - 1);
        s_pending.count++;
    }

    if (s_write_timer != NULL) {
        /* Re-arm: the delay restarts from the most recent change. */
        lv_timer_reset(s_write_timer);
        return;
    }

    s_write_timer = lv_timer_create(write_timer_cb, PREFS_WRITE_DELAY_MS, NULL);
    if (s_write_timer == NULL) {
        /* No timer to defer with; the write is worth more than the stall. */
        write_now();
        return;
    }
    lv_timer_set_repeat_count(s_write_timer, 1);
}
