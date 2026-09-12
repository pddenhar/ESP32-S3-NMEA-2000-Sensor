#include <math.h>
#include <stdio.h>

#include "ui_dial_gauge.h"
#include "ui_theme.h"

/* Three quarters of a circle, opening at the bottom: the shape of nearly every
 * engine instrument, and the gap leaves the needle clear of the readout. */
#define GAUGE_ANGLE_RANGE 270
#define GAUGE_ROTATION    135
#define GAUGE_TICKS_PER_MAJOR 5

/* The dial is kept clear of the bottom of the container so the readout sits
 * under it rather than behind the needle, which is drawn from the centre out. */
#define GAUGE_DIAL_INSET 100

/* Shared by every dial; the sections that use them are per gauge. */
static lv_style_t s_warn_items;
static lv_style_t s_warn_main;
static bool s_styles_ready;

static void styles_init(void)
{
    if (s_styles_ready) {
        return;
    }

    lv_style_init(&s_warn_items);
    lv_style_set_line_color(&s_warn_items, UI_COLOR_BAD);
    lv_style_set_line_width(&s_warn_items, 4);
    lv_style_set_text_color(&s_warn_items, UI_COLOR_BAD);

    lv_style_init(&s_warn_main);
    lv_style_set_arc_color(&s_warn_main, UI_COLOR_BAD);
    lv_style_set_arc_width(&s_warn_main, 5);

    s_styles_ready = true;
}

static bool value_is_alarming(const ui_dial_gauge_t *gauge, int32_t value)
{
    const ui_dial_spec_t *spec = gauge->spec;
    if (spec->warn_below != UI_DIAL_NO_WARN && value <= spec->warn_below) {
        return true;
    }
    if (spec->warn_above != UI_DIAL_NO_WARN && value >= spec->warn_above) {
        return true;
    }
    return false;
}

static void tick_labels_init(ui_dial_gauge_t *gauge)
{
    const ui_dial_spec_t *spec = gauge->spec;
    const int intervals = spec->major_ticks - 1;

    for (int i = 0; i < spec->major_ticks; i++) {
        const int32_t value = spec->min + (int32_t)((int64_t)(spec->max - spec->min) * i / intervals);
        snprintf(gauge->tick_labels[i], sizeof(gauge->tick_labels[i]), "%d", (int)value);
        gauge->tick_label_ptrs[i] = gauge->tick_labels[i];
    }
    gauge->tick_label_ptrs[spec->major_ticks] = NULL;
}

static void update_readout(ui_dial_gauge_t *gauge)
{
    if (!gauge->valid) {
        lv_label_set_text(gauge->value_label, "--");
        lv_obj_set_style_text_color(gauge->value_label, UI_COLOR_DIM, 0);
        lv_obj_set_style_line_color(gauge->needle, UI_COLOR_DIM, 0);
        lv_scale_set_line_needle_value(gauge->scale, gauge->needle, -20, gauge->spec->min);
        return;
    }

    const bool alarming = value_is_alarming(gauge, gauge->value);
    const lv_color_t color = alarming ? UI_COLOR_BAD : UI_COLOR_FG;

    lv_label_set_text_fmt(gauge->value_label, "%d %s", (int)gauge->value, gauge->spec->units);
    lv_obj_set_style_text_color(gauge->value_label, color, 0);
    lv_obj_set_style_line_color(gauge->needle, color, 0);
    lv_scale_set_line_needle_value(gauge->scale, gauge->needle, -20, gauge->value);
}

void ui_dial_gauge_create(ui_dial_gauge_t *gauge, lv_obj_t *parent, int32_t size,
                          const ui_dial_spec_t *spec)
{
    styles_init();

    lv_memzero(gauge, sizeof(*gauge));
    gauge->spec = spec;
    gauge->valid = false;

    gauge->cont = lv_obj_create(parent);
    lv_obj_set_size(gauge->cont, size, size);
    lv_obj_remove_flag(gauge->cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(gauge->cont, UI_COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(gauge->cont, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(gauge->cont, 2, 0);
    lv_obj_set_style_radius(gauge->cont, 12, 0);
    lv_obj_set_style_pad_all(gauge->cont, 8, 0);

    const int major_ticks = (spec->major_ticks < 2)                    ? 2
                            : (spec->major_ticks > UI_DIAL_MAX_MAJOR_TICKS)
                                ? UI_DIAL_MAX_MAJOR_TICKS
                                : spec->major_ticks;
    const int total_ticks = (major_ticks - 1) * GAUGE_TICKS_PER_MAJOR + 1;

    gauge->scale = lv_scale_create(gauge->cont);
    lv_obj_set_size(gauge->scale, size - GAUGE_DIAL_INSET, size - GAUGE_DIAL_INSET);
    lv_obj_align(gauge->scale, LV_ALIGN_TOP_MID, 0, 0);
    lv_scale_set_mode(gauge->scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_angle_range(gauge->scale, GAUGE_ANGLE_RANGE);
    lv_scale_set_rotation(gauge->scale, GAUGE_ROTATION);
    lv_scale_set_total_tick_count(gauge->scale, total_ticks);
    lv_scale_set_major_tick_every(gauge->scale, GAUGE_TICKS_PER_MAJOR);
    lv_scale_set_range(gauge->scale, spec->min, spec->max);
    lv_scale_set_label_show(gauge->scale, true);

    tick_labels_init(gauge);
    lv_scale_set_text_src(gauge->scale, gauge->tick_label_ptrs);

    lv_obj_set_style_bg_opa(gauge->scale, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gauge->scale, 0, 0);
    lv_obj_set_style_pad_all(gauge->scale, 4, 0);
    lv_obj_set_style_text_color(gauge->scale, UI_COLOR_FG, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(gauge->scale, &lv_font_montserrat_14, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(gauge->scale, UI_COLOR_FG, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(gauge->scale, 3, LV_PART_INDICATOR);
    lv_obj_set_style_length(gauge->scale, 12, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(gauge->scale, UI_COLOR_DIM, LV_PART_ITEMS);
    lv_obj_set_style_line_width(gauge->scale, 2, LV_PART_ITEMS);
    lv_obj_set_style_length(gauge->scale, 6, LV_PART_ITEMS);
    lv_obj_set_style_arc_color(gauge->scale, UI_COLOR_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gauge->scale, 3, LV_PART_MAIN);

    /* The alarm bands are on the dial as well as in the readout's colour, so
     * the needle's distance from trouble can be seen without reading it. */
    if (spec->warn_below != UI_DIAL_NO_WARN && spec->warn_below > spec->min) {
        lv_scale_section_t *low = lv_scale_add_section(gauge->scale);
        lv_scale_set_section_range(gauge->scale, low, spec->min, spec->warn_below);
        lv_scale_set_section_style_items(gauge->scale, low, &s_warn_items);
        lv_scale_set_section_style_main(gauge->scale, low, &s_warn_main);
    }
    if (spec->warn_above != UI_DIAL_NO_WARN && spec->warn_above < spec->max) {
        lv_scale_section_t *high = lv_scale_add_section(gauge->scale);
        lv_scale_set_section_range(gauge->scale, high, spec->warn_above, spec->max);
        lv_scale_set_section_style_items(gauge->scale, high, &s_warn_items);
        lv_scale_set_section_style_main(gauge->scale, high, &s_warn_main);
    }

    gauge->needle = lv_line_create(gauge->scale);
    lv_obj_set_style_line_width(gauge->needle, 6, 0);
    lv_obj_set_style_line_rounded(gauge->needle, true, 0);

    gauge->value_label = lv_label_create(gauge->cont);
    lv_obj_set_style_text_font(gauge->value_label, &lv_font_montserrat_28, 0);
    lv_obj_align(gauge->value_label, LV_ALIGN_BOTTOM_MID, 0, -30);

    gauge->title_label = lv_label_create(gauge->cont);
    lv_obj_set_style_text_font(gauge->title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gauge->title_label, UI_COLOR_DIM, 0);
    lv_label_set_text(gauge->title_label, "");
    lv_obj_align(gauge->title_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    update_readout(gauge);
}

void ui_dial_gauge_set_title(ui_dial_gauge_t *gauge, const char *title)
{
    lv_label_set_text(gauge->title_label, title);
}

void ui_dial_gauge_set_value(ui_dial_gauge_t *gauge, int32_t value)
{
    if (value > gauge->spec->max) {
        value = gauge->spec->max;
    } else if (value < gauge->spec->min) {
        value = gauge->spec->min;
    }

    if (gauge->valid && gauge->value == value) {
        return;
    }

    gauge->value = value;
    gauge->valid = true;
    update_readout(gauge);
}

void ui_dial_gauge_set_no_data(ui_dial_gauge_t *gauge)
{
    if (!gauge->valid) {
        return;
    }

    gauge->valid = false;
    update_readout(gauge);
}

/* ---------------------------------------------------------------------------
 * Gauge class adapter; see the note in ui_rudder_gauge.c on tile ownership.
 * ------------------------------------------------------------------------- */

static void gauge_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_dial_gauge_class_create(lv_obj_t *parent, int32_t size, const char *title,
                                     const ui_dial_spec_t *spec)
{
    ui_dial_gauge_t *gauge = lv_malloc(sizeof(ui_dial_gauge_t));
    if (gauge == NULL) {
        return NULL;
    }

    ui_dial_gauge_create(gauge, parent, size, spec);
    ui_dial_gauge_set_title(gauge, title);
    lv_obj_set_user_data(gauge->cont, gauge);
    lv_obj_add_event_cb(gauge->cont, gauge_free_cb, LV_EVENT_DELETE, gauge);
    return gauge->cont;
}

void ui_dial_gauge_class_update(lv_obj_t *tile, const n2k_reading_t *reading)
{
    ui_dial_gauge_t *gauge = lv_obj_get_user_data(tile);

    if (reading->valid) {
        ui_dial_gauge_set_value(gauge, (int32_t)lroundf(reading->value));
    } else {
        ui_dial_gauge_set_no_data(gauge);
    }
}
