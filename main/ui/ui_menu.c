#include "ui_menu.h"
#include "ui_theme.h"

#include <stdio.h>

#define MENU_WIDTH       320
#define MENU_ANIM_MS     180
#define MENU_ROW_HEIGHT  60

/* How often the live markers are refreshed. The timer only exists while the
 * drawer is open, so a closed menu costs nothing. */
#define MENU_LIVE_POLL_MS 500

#define LIVE_DOT_SIZE 12

static const ui_gauge_class_t *const *s_classes;
static int s_max_selected;
static ui_menu_apply_cb_t s_on_apply;

static lv_obj_t *s_scrim;
static lv_obj_t *s_panel;
static lv_obj_t *s_footer;
static lv_obj_t *s_checkbox[N2K_CH_COUNT]; /* NULL for channels with no gauge */
static lv_obj_t *s_live_dot[N2K_CH_COUNT];
static lv_timer_t *s_live_timer;
static bool s_is_open;

/* Selection in tile order: the first entry is the left tile. */
static n2k_channel_id_t s_selected[N2K_CH_COUNT];
static int s_selected_count;

static bool is_selected(n2k_channel_id_t id)
{
    for (int i = 0; i < s_selected_count; i++) {
        if (s_selected[i] == id) {
            return true;
        }
    }
    return false;
}

static void selection_remove(n2k_channel_id_t id)
{
    for (int i = 0; i < s_selected_count; i++) {
        if (s_selected[i] == id) {
            for (int j = i; j + 1 < s_selected_count; j++) {
                s_selected[j] = s_selected[j + 1];
            }
            s_selected_count--;
            return;
        }
    }
}

/* Unchecked rows are shown as unavailable once the panel is full, rather than
 * having a new choice silently evict a gauge the user is watching. */
static void refresh_availability(void)
{
    const bool full = (s_selected_count >= s_max_selected);

    for (int id = 0; id < N2K_CH_COUNT; id++) {
        if (s_checkbox[id] == NULL) {
            continue;
        }
        if (full && !is_selected((n2k_channel_id_t)id)) {
            lv_obj_add_state(s_checkbox[id], LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(s_checkbox[id], LV_STATE_DISABLED);
        }
    }

    if (full) {
        /* Bullet, not an em dash: see the note on glyph coverage in ui_theme.h. */
        lv_label_set_text_fmt(s_footer, "%d of %d shown \xE2\x80\xA2 deselect one to swap",
                              s_selected_count, s_max_selected);
    } else {
        lv_label_set_text_fmt(s_footer, "%d of %d shown", s_selected_count, s_max_selected);
    }
}

static void live_refresh(void)
{
    for (int id = 0; id < N2K_CH_COUNT; id++) {
        if (s_live_dot[id] == NULL) {
            continue;
        }
        n2k_reading_t reading;
        const bool live = (n2k_channels_read((n2k_channel_id_t)id, &reading) == ESP_OK) &&
                          reading.valid;
        lv_obj_set_style_bg_color(s_live_dot[id], live ? UI_COLOR_OK : UI_COLOR_BORDER, 0);
    }
}

static void live_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    live_refresh();
}

static void checkbox_event_cb(lv_event_t *e)
{
    lv_obj_t *checkbox = lv_event_get_target(e);
    const n2k_channel_id_t id = (n2k_channel_id_t)(intptr_t)lv_event_get_user_data(e);
    const bool checked = lv_obj_has_state(checkbox, LV_STATE_CHECKED);

    if (checked) {
        /* Guard as well as disable: whether a disabled widget can be clicked is
         * a detail of the input layer, and the cap has to hold either way. */
        if (s_selected_count >= s_max_selected) {
            lv_obj_remove_state(checkbox, LV_STATE_CHECKED);
            return;
        }
        if (!is_selected(id)) {
            s_selected[s_selected_count++] = id;
        }
    } else {
        selection_remove(id);
    }

    refresh_availability();
    if (s_on_apply != NULL) {
        s_on_apply(s_selected, s_selected_count);
    }
}

static void scrim_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_menu_close();
}

static void close_button_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_menu_close();
}

static void row_create(lv_obj_t *parent, n2k_channel_id_t id, const n2k_channel_desc_t *desc)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), MENU_ROW_HEIGHT);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(row, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t *checkbox = lv_checkbox_create(row);
    lv_checkbox_set_text(checkbox, desc->name);
    lv_obj_set_style_text_font(checkbox, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(checkbox, UI_COLOR_FG, 0);
    lv_obj_set_style_text_color(checkbox, UI_COLOR_DIM, LV_STATE_DISABLED);
    lv_obj_align(checkbox, LV_ALIGN_TOP_LEFT, 0, 6);
    lv_obj_add_event_cb(checkbox, checkbox_event_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)id);
    s_checkbox[id] = checkbox;

    /* The PGN under the name: it is what identifies the message in a capture,
     * and the name alone is ambiguous once similar PGNs are added. */
    lv_obj_t *pgn = lv_label_create(row);
    lv_label_set_text_fmt(pgn, "PGN %lu", desc->pgn);
    lv_obj_set_style_text_font(pgn, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pgn, UI_COLOR_DIM, 0);
    lv_obj_align(pgn, LV_ALIGN_TOP_LEFT, 30, 30);

    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_set_size(dot, LIVE_DOT_SIZE, LIVE_DOT_SIZE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_color(dot, UI_COLOR_BORDER, 0);
    lv_obj_align(dot, LV_ALIGN_RIGHT_MID, -4, 0);
    s_live_dot[id] = dot;
}

void ui_menu_create(lv_obj_t *parent,
                    const ui_gauge_class_t *const *gauge_classes,
                    int max_selected,
                    ui_menu_apply_cb_t on_apply)
{
    s_classes = gauge_classes;
    s_max_selected = (max_selected > N2K_CH_COUNT) ? N2K_CH_COUNT : max_selected;
    s_on_apply = on_apply;

    /* Covers the whole screen so a tap anywhere outside the drawer closes it,
     * and so nothing behind the drawer can be operated by accident. */
    s_scrim = lv_obj_create(parent);
    lv_obj_set_size(s_scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_scrim, 0, 0);
    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_scrim, 0, 0);
    lv_obj_set_style_radius(s_scrim, 0, 0);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_scrim, scrim_event_cb, LV_EVENT_CLICKED, NULL);

    s_panel = lv_obj_create(parent);
    lv_obj_set_size(s_panel, MENU_WIDTH, LV_PCT(100));
    lv_obj_set_pos(s_panel, -MENU_WIDTH, 0);
    lv_obj_set_style_bg_color(s_panel, UI_COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(s_panel, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_side(s_panel, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 16, 0);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    /* Laid out as a column -- title, list, footer -- so the list takes whatever
     * height is left instead of being told a number that has to be kept in step
     * with the panel's padding. */
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_panel, 8, 0);

    lv_obj_t *title_row = lv_obj_create(s_panel);
    lv_obj_set_size(title_row, LV_PCT(100), 24);
    lv_obj_remove_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);

    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, "GAUGES");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_DIM, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *close = lv_label_create(title_row);
    lv_label_set_text(close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close, UI_COLOR_DIM, 0);
    lv_obj_align(close, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE);
    /* Generous touch target around a small glyph. */
    lv_obj_set_ext_click_area(close, 16);
    lv_obj_add_event_cb(close, close_button_event_cb, LV_EVENT_CLICKED, NULL);

    /* The list scrolls, so the drawer keeps working as channels are added. */
    lv_obj_t *list = lv_obj_create(s_panel);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    for (int id = 0; id < N2K_CH_COUNT; id++) {
        const n2k_channel_desc_t *desc = n2k_channels_desc((n2k_channel_id_t)id);
        /* A channel with no gauge class is decoded but not yet drawable, so it
         * is left out rather than offered as a choice that does nothing. */
        if (desc == NULL || s_classes[id] == NULL) {
            continue;
        }
        row_create(list, (n2k_channel_id_t)id, desc);
    }

    s_footer = lv_label_create(s_panel);
    lv_obj_set_width(s_footer, LV_PCT(100));
    lv_obj_set_style_text_font(s_footer, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_footer, UI_COLOR_DIM, 0);

    refresh_availability();
}

static void anim_x_cb(void *obj, int32_t value)
{
    lv_obj_set_x(obj, value);
}

void ui_menu_open(void)
{
    if (s_panel == NULL || s_is_open) {
        return;
    }
    s_is_open = true;

    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    /* Raise above whatever has been created since, so the drawer is never
     * drawn under a tile. */
    lv_obj_move_foreground(s_scrim);
    lv_obj_move_foreground(s_panel);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_panel);
    lv_anim_set_exec_cb(&anim, anim_x_cb);
    lv_anim_set_values(&anim, lv_obj_get_x(s_panel), 0);
    lv_anim_set_duration(&anim, MENU_ANIM_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);

    live_refresh();
    if (s_live_timer == NULL) {
        s_live_timer = lv_timer_create(live_timer_cb, MENU_LIVE_POLL_MS, NULL);
    }
}

static void close_anim_done_cb(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    /* Hidden only once it is off screen: hiding first would make it vanish
     * rather than slide. */
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
}

void ui_menu_close(void)
{
    if (s_panel == NULL || !s_is_open) {
        return;
    }
    s_is_open = false;

    if (s_live_timer != NULL) {
        lv_timer_delete(s_live_timer);
        s_live_timer = NULL;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_panel);
    lv_anim_set_exec_cb(&anim, anim_x_cb);
    lv_anim_set_values(&anim, lv_obj_get_x(s_panel), -MENU_WIDTH);
    lv_anim_set_duration(&anim, MENU_ANIM_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&anim, close_anim_done_cb);
    lv_anim_start(&anim);
}

void ui_menu_set_selection(const n2k_channel_id_t *ids, int count)
{
    s_selected_count = 0;
    for (int i = 0; i < count && s_selected_count < s_max_selected; i++) {
        if (ids[i] >= 0 && ids[i] < N2K_CH_COUNT && s_checkbox[ids[i]] != NULL) {
            s_selected[s_selected_count++] = ids[i];
        }
    }

    for (int id = 0; id < N2K_CH_COUNT; id++) {
        if (s_checkbox[id] == NULL) {
            continue;
        }
        if (is_selected((n2k_channel_id_t)id)) {
            lv_obj_add_state(s_checkbox[id], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_checkbox[id], LV_STATE_CHECKED);
        }
    }

    refresh_availability();
}
