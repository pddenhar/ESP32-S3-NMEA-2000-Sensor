#include "ui_main.h"
#include "ui_engine_gauges.h"
#include "ui_gauge.h"
#include "ui_heading_gauge.h"
#include "ui_menu.h"
#include "ui_prefs.h"
#include "ui_rudder_gauge.h"
#include "ui_theme.h"
#include "n2k_bridge.h"
#include "n2k_channels.h"

/* How often the visible tiles are refreshed. The fastest channel (the local
 * rudder sensor) measures at ~100 Hz, and there is nothing to gain from
 * redrawing faster than the eye resolves. The slower channels cost nothing to
 * poll at the same rate, because a gauge handed an unchanged value returns
 * without touching the display. */
#define UI_TILE_POLL_MS 100

/* The bus state changes over seconds, so it is polled far more slowly. */
#define UI_STATUS_POLL_MS 500

#define UI_HEADER_HEIGHT 56

/* Tiles are square and sized to how many are on screen: one gets the room two
 * would have shared. */
#define UI_MAX_TILES 2
#define UI_TILE_SIZE_SINGLE 380
#define UI_TILE_SIZE_PAIR 320

/**
 * Which widget draws each channel.
 *
 * This table is the one place the data side and the display side meet. A NULL
 * entry means the channel is decoded but has no gauge yet, which is a
 * legitimate state: it simply stays out of the menu until one is written.
 */
static const ui_gauge_class_t *const s_gauge_classes[N2K_CH_COUNT] = {
    [N2K_CH_RUDDER] = &ui_gauge_rudder,
    [N2K_CH_HEADING] = &ui_gauge_compass,
    [N2K_CH_ENGINE_SPEED] = &ui_gauge_engine_speed,
    [N2K_CH_ENGINE_TEMP] = &ui_gauge_engine_temp,
    [N2K_CH_ENGINE_OIL_PRESSURE] = &ui_gauge_oil_pressure,
};

/* Shown on a first boot, or when nothing usable was saved. */
static const n2k_channel_id_t s_default_channels[] = {N2K_CH_RUDDER, N2K_CH_HEADING};

static bool channel_is_displayable(n2k_channel_id_t id)
{
    return id >= 0 && id < N2K_CH_COUNT && s_gauge_classes[id] != NULL &&
           n2k_channels_desc(id) != NULL;
}

static lv_obj_t *s_tile_area;
static lv_obj_t *s_bus_status_label;

static n2k_channel_id_t s_visible[UI_MAX_TILES];
static lv_obj_t *s_tiles[UI_MAX_TILES];
static int s_visible_count;

/* Runs in the LVGL task, which already holds the lock. */
static void tile_poll_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    for (int i = 0; i < s_visible_count; i++) {
        if (s_tiles[i] == NULL) {
            continue;
        }
        n2k_reading_t reading;
        if (n2k_channels_read(s_visible[i], &reading) != ESP_OK) {
            continue;
        }
        s_gauge_classes[s_visible[i]]->update(s_tiles[i], &reading);
    }
}

/* Redraws the bus status line only when it actually changes: the label is
 * repainted by LVGL on every set_text, change or not. */
static void bus_status_poll_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    static bool initialised;
    static n2k_bridge_status_t last;

    n2k_bridge_status_t status;
    n2k_bridge_get_status(&status);

    if (initialised && status.state == last.state && status.address == last.address) {
        return;
    }
    last = status;
    initialised = true;

    lv_color_t color;
    switch (status.state) {
    case N2K_BUS_ONLINE:
        /* The claimed source address is worth showing: it is what identifies
         * this device in a capture or on another display's device list. */
        lv_label_set_text_fmt(s_bus_status_label, "NMEA 2000: online (addr %d)",
                              (int)status.address);
        color = UI_COLOR_OK;
        break;
    case N2K_BUS_CLAIMING:
        lv_label_set_text(s_bus_status_label, "NMEA 2000: claiming address");
        color = UI_COLOR_WARN;
        break;
    case N2K_BUS_ERROR:
        /* Error-passive: frames are going out but nothing is acknowledging
         * them. Usually an unplugged drop cable or a missing terminator. */
        lv_label_set_text(s_bus_status_label, "NMEA 2000: no response");
        color = UI_COLOR_WARN;
        break;
    case N2K_BUS_OFF:
        lv_label_set_text(s_bus_status_label, "NMEA 2000: bus off");
        color = UI_COLOR_BAD;
        break;
    case N2K_BUS_STOPPED:
    default:
        lv_label_set_text(s_bus_status_label, "NMEA 2000: offline");
        color = UI_COLOR_DIM;
        break;
    }
    lv_obj_set_style_text_color(s_bus_status_label, color, 0);
    lv_obj_align(s_bus_status_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* Discards the current tiles and builds the selected ones. Cheap enough to do
 * wholesale: it happens when a human taps a checkbox, not on every frame. */
static void tiles_rebuild(void)
{
    for (int i = 0; i < UI_MAX_TILES; i++) {
        if (s_tiles[i] != NULL) {
            /* The widget's own struct is freed from its delete handler. */
            lv_obj_delete(s_tiles[i]);
            s_tiles[i] = NULL;
        }
    }

    const int32_t size = (s_visible_count > 1) ? UI_TILE_SIZE_PAIR : UI_TILE_SIZE_SINGLE;

    for (int i = 0; i < s_visible_count; i++) {
        const n2k_channel_desc_t *desc = n2k_channels_desc(s_visible[i]);
        if (desc == NULL || s_gauge_classes[s_visible[i]] == NULL) {
            continue;
        }
        s_tiles[i] = s_gauge_classes[s_visible[i]]->create(s_tile_area, size, desc->name);
    }

    /* Fill the new tiles straight away rather than leaving them blank until
     * the next poll comes round. */
    tile_poll_cb(NULL);
}

static void menu_apply_cb(const n2k_channel_id_t *ids, int count)
{
    if (count > UI_MAX_TILES) {
        count = UI_MAX_TILES;
    }

    s_visible_count = 0;
    for (int i = 0; i < count; i++) {
        if (channel_is_displayable(ids[i])) {
            s_visible[s_visible_count++] = ids[i];
        }
    }

    tiles_rebuild();
    ui_prefs_save_channels(s_visible, s_visible_count);
}

static void menu_button_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_menu_open();
}

static void header_create(lv_obj_t *parent)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), UI_HEADER_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, UI_COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 16, 0);

    lv_obj_t *menu_button = lv_label_create(header);
    lv_label_set_text(menu_button, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(menu_button, UI_COLOR_FG, 0);
    lv_obj_align(menu_button, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(menu_button, LV_OBJ_FLAG_CLICKABLE);
    /* The glyph is small; the area that answers a thumb should not be. */
    lv_obj_set_ext_click_area(menu_button, 20);
    lv_obj_add_event_cb(menu_button, menu_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_FG, 0);
    lv_label_set_text(title, "Sensor Bridge");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 36, 0);

    s_bus_status_label = lv_label_create(header);
    lv_obj_set_style_text_font(s_bus_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_bus_status_label, UI_COLOR_DIM, 0);
    lv_label_set_text(s_bus_status_label, "NMEA 2000: offline");
    lv_obj_align(s_bus_status_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* The tiles live in a flex row that spaces them out itself, so adding or
 * removing one needs no layout arithmetic. */
static void tile_area_create(lv_obj_t *parent)
{
    LV_UNUSED(parent);

    s_tile_area = lv_obj_create(parent);
    lv_obj_set_size(s_tile_area,
                    lv_display_get_horizontal_resolution(NULL),
                    lv_display_get_vertical_resolution(NULL) - UI_HEADER_HEIGHT);
    lv_obj_set_pos(s_tile_area, 0, UI_HEADER_HEIGHT);
    lv_obj_remove_flag(s_tile_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_tile_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tile_area, 0, 0);
    lv_obj_set_style_pad_all(s_tile_area, 0, 0);
    lv_obj_set_flex_flow(s_tile_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_tile_area, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

void ui_main_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, UI_COLOR_SCREEN_BG, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    header_create(screen);
    tile_area_create(screen);

    /* Created after the tile area so the drawer stacks above it. */
    ui_menu_create(screen, s_gauge_classes, UI_MAX_TILES, menu_apply_cb);

    n2k_channel_id_t restored[UI_MAX_TILES];
    int restored_count = 0;
    if (ui_prefs_load_channels(restored, &restored_count, UI_MAX_TILES) != ESP_OK) {
        restored_count = 0;
    }

    /* A saved channel whose gauge has since been removed is dropped here: the
     * selection has to survive the firmware changing under it. */
    s_visible_count = 0;
    for (int i = 0; i < restored_count; i++) {
        if (channel_is_displayable(restored[i])) {
            s_visible[s_visible_count++] = restored[i];
        }
    }
    if (s_visible_count == 0) {
        for (size_t i = 0;
             i < sizeof(s_default_channels) / sizeof(s_default_channels[0]) &&
             s_visible_count < UI_MAX_TILES;
             i++) {
            if (channel_is_displayable(s_default_channels[i])) {
                s_visible[s_visible_count++] = s_default_channels[i];
            }
        }
    }

    ui_menu_set_selection(s_visible, s_visible_count);
    tiles_rebuild();

    lv_timer_create(tile_poll_cb, UI_TILE_POLL_MS, NULL);
    lv_timer_create(bus_status_poll_cb, UI_STATUS_POLL_MS, NULL);
}
