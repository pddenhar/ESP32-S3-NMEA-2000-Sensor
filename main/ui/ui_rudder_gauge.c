#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ui_rudder_gauge.h"

/* The scale sweeps the top half of a circle: 180 deg of arc, starting on the
 * left. Six intervals between the seven labelled ticks keeps the numbers round
 * for the usual +/-30 and +/-45 deg rudder stops. */
#define GAUGE_ANGLE_RANGE   180
#define GAUGE_ROTATION      180
#define GAUGE_MAJOR_TICKS   7
#define GAUGE_TICKS_PER_MAJOR 5
#define GAUGE_TOTAL_TICKS   ((GAUGE_MAJOR_TICKS - 1) * GAUGE_TICKS_PER_MAJOR + 1)

#define COLOR_PORT          lv_color_hex(0xE03030)
#define COLOR_STBD          lv_color_hex(0x30B050)
#define COLOR_BG            lv_color_hex(0x1B1F25)
#define COLOR_FG            lv_color_hex(0xE8ECF0)
#define COLOR_DIM           lv_color_hex(0x8A939E)

/* Section styles are referenced by the scale for its lifetime, so they are
 * shared statics rather than per-instance copies. */
static lv_style_t s_port_items;
static lv_style_t s_port_main;
static lv_style_t s_stbd_items;
static lv_style_t s_stbd_main;
static bool s_styles_ready;

static void styles_init(void)
{
    if (s_styles_ready) {
        return;
    }

    lv_style_init(&s_port_items);
    lv_style_set_line_color(&s_port_items, COLOR_PORT);
    lv_style_set_line_width(&s_port_items, 4);
    lv_style_set_text_color(&s_port_items, COLOR_PORT);

    lv_style_init(&s_port_main);
    lv_style_set_arc_color(&s_port_main, COLOR_PORT);
    lv_style_set_arc_width(&s_port_main, 5);

    lv_style_init(&s_stbd_items);
    lv_style_set_line_color(&s_stbd_items, COLOR_STBD);
    lv_style_set_line_width(&s_stbd_items, 4);
    lv_style_set_text_color(&s_stbd_items, COLOR_STBD);

    lv_style_init(&s_stbd_main);
    lv_style_set_arc_color(&s_stbd_main, COLOR_STBD);
    lv_style_set_arc_width(&s_stbd_main, 5);

    s_styles_ready = true;
}

/* Tick labels count outwards from zero on both sides, the way a rudder
 * indicator is read: 45 30 15 0 15 30 45. */
static void tick_labels_init(ui_rudder_gauge_t *gauge)
{
    const int32_t step = gauge->range_deg / ((GAUGE_MAJOR_TICKS - 1) / 2);

    for (int i = 0; i < GAUGE_MAJOR_TICKS; i++) {
        int magnitude = abs(i - (GAUGE_MAJOR_TICKS - 1) / 2) * (int)step;
        /* Bounded so the label always fits its buffer (and to keep the
         * compiler's format-truncation analysis happy). */
        if (magnitude < 0) {
            magnitude = 0;
        } else if (magnitude > 999) {
            magnitude = 999;
        }
        snprintf(gauge->tick_labels[i], sizeof(gauge->tick_labels[i]), "%d", magnitude);
        gauge->tick_label_ptrs[i] = gauge->tick_labels[i];
    }
    gauge->tick_label_ptrs[GAUGE_MAJOR_TICKS] = NULL;
}

static void update_readout(ui_rudder_gauge_t *gauge)
{
    if (!gauge->valid) {
        lv_label_set_text(gauge->value_label, "--");
        lv_obj_set_style_text_color(gauge->value_label, COLOR_DIM, 0);
        lv_obj_set_style_line_color(gauge->needle, COLOR_DIM, 0);
        lv_scale_set_line_needle_value(gauge->scale, gauge->needle, -18, 0);
        return;
    }

    int32_t v = gauge->value_deg;
    if (v < 0) {
        lv_label_set_text_fmt(gauge->value_label, "P %d\xC2\xB0", (int)-v);
        lv_obj_set_style_text_color(gauge->value_label, COLOR_PORT, 0);
        lv_obj_set_style_line_color(gauge->needle, COLOR_PORT, 0);
    } else if (v > 0) {
        lv_label_set_text_fmt(gauge->value_label, "S %d\xC2\xB0", (int)v);
        lv_obj_set_style_text_color(gauge->value_label, COLOR_STBD, 0);
        lv_obj_set_style_line_color(gauge->needle, COLOR_STBD, 0);
    } else {
        lv_label_set_text(gauge->value_label, "0\xC2\xB0");
        lv_obj_set_style_text_color(gauge->value_label, COLOR_FG, 0);
        lv_obj_set_style_line_color(gauge->needle, COLOR_FG, 0);
    }

    lv_scale_set_line_needle_value(gauge->scale, gauge->needle, -18, v);
}

void ui_rudder_gauge_create(ui_rudder_gauge_t *gauge, lv_obj_t *parent, int32_t size, int32_t range_deg)
{
    styles_init();

    lv_memzero(gauge, sizeof(*gauge));
    gauge->range_deg = (range_deg > 0 && range_deg <= 180) ? range_deg : 45;
    gauge->valid = false;

    gauge->cont = lv_obj_create(parent);
    lv_obj_set_size(gauge->cont, size, size);
    lv_obj_remove_flag(gauge->cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(gauge->cont, COLOR_BG, 0);
    lv_obj_set_style_border_color(gauge->cont, lv_color_hex(0x2E353E), 0);
    lv_obj_set_style_border_width(gauge->cont, 2, 0);
    lv_obj_set_style_radius(gauge->cont, 12, 0);
    lv_obj_set_style_pad_all(gauge->cont, 8, 0);

    /* The half-circle scale only paints the upper half of its bounding box, so
     * give it a square box and pull it up to leave room for the readout. */
    gauge->scale = lv_scale_create(gauge->cont);
    lv_obj_set_size(gauge->scale, size - 40, size - 40);
    lv_obj_align(gauge->scale, LV_ALIGN_TOP_MID, 0, 0);
    lv_scale_set_mode(gauge->scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_angle_range(gauge->scale, GAUGE_ANGLE_RANGE);
    lv_scale_set_rotation(gauge->scale, GAUGE_ROTATION);
    lv_scale_set_total_tick_count(gauge->scale, GAUGE_TOTAL_TICKS);
    lv_scale_set_major_tick_every(gauge->scale, GAUGE_TICKS_PER_MAJOR);
    lv_scale_set_range(gauge->scale, -gauge->range_deg, gauge->range_deg);
    lv_scale_set_label_show(gauge->scale, true);

    tick_labels_init(gauge);
    lv_scale_set_text_src(gauge->scale, gauge->tick_label_ptrs);

    lv_obj_set_style_bg_opa(gauge->scale, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gauge->scale, 0, 0);
    lv_obj_set_style_pad_all(gauge->scale, 4, 0);
    lv_obj_set_style_text_color(gauge->scale, COLOR_FG, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(gauge->scale, COLOR_FG, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(gauge->scale, 4, LV_PART_INDICATOR);
    lv_obj_set_style_length(gauge->scale, 14, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(gauge->scale, COLOR_DIM, LV_PART_ITEMS);
    lv_obj_set_style_line_width(gauge->scale, 2, LV_PART_ITEMS);
    lv_obj_set_style_length(gauge->scale, 8, LV_PART_ITEMS);
    lv_obj_set_style_arc_color(gauge->scale, COLOR_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gauge->scale, 3, LV_PART_MAIN);

    gauge->port_section = lv_scale_add_section(gauge->scale);
    lv_scale_set_section_range(gauge->scale, gauge->port_section, -gauge->range_deg, 0);
    lv_scale_set_section_style_items(gauge->scale, gauge->port_section, &s_port_items);
    lv_scale_set_section_style_main(gauge->scale, gauge->port_section, &s_port_main);

    gauge->stbd_section = lv_scale_add_section(gauge->scale);
    lv_scale_set_section_range(gauge->scale, gauge->stbd_section, 0, gauge->range_deg);
    lv_scale_set_section_style_items(gauge->scale, gauge->stbd_section, &s_stbd_items);
    lv_scale_set_section_style_main(gauge->scale, gauge->stbd_section, &s_stbd_main);

    gauge->needle = lv_line_create(gauge->scale);
    lv_obj_set_style_line_width(gauge->needle, 6, 0);
    lv_obj_set_style_line_rounded(gauge->needle, true, 0);

    gauge->value_label = lv_label_create(gauge->cont);
    lv_obj_set_style_text_font(gauge->value_label, &lv_font_montserrat_28, 0);
    lv_obj_align(gauge->value_label, LV_ALIGN_BOTTOM_MID, 0, -26);

    gauge->title_label = lv_label_create(gauge->cont);
    lv_obj_set_style_text_font(gauge->title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gauge->title_label, COLOR_DIM, 0);
    lv_label_set_text(gauge->title_label, "RUDDER");
    lv_obj_align(gauge->title_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    update_readout(gauge);
}

void ui_rudder_gauge_set_title(ui_rudder_gauge_t *gauge, const char *title)
{
    lv_label_set_text(gauge->title_label, title);
}

void ui_rudder_gauge_set_value(ui_rudder_gauge_t *gauge, int32_t angle_deg)
{
    if (angle_deg > gauge->range_deg) {
        angle_deg = gauge->range_deg;
    } else if (angle_deg < -gauge->range_deg) {
        angle_deg = -gauge->range_deg;
    }

    if (gauge->valid && gauge->value_deg == angle_deg) {
        return;
    }

    gauge->value_deg = angle_deg;
    gauge->valid = true;
    update_readout(gauge);
}

void ui_rudder_gauge_set_no_data(ui_rudder_gauge_t *gauge)
{
    if (!gauge->valid) {
        return;
    }

    gauge->valid = false;
    update_readout(gauge);
}

/* ---------------------------------------------------------------------------
 * Gauge class adapter
 *
 * The struct is owned by the tile: allocated here, reachable through the tile's
 * user data, and freed when LVGL deletes the tile. That is what lets the panel
 * create and destroy tiles as the selection changes without tracking storage
 * for each one.
 * ------------------------------------------------------------------------- */

static void gauge_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static lv_obj_t *gauge_create(lv_obj_t *parent, int32_t size, const char *title)
{
    ui_rudder_gauge_t *gauge = lv_malloc(sizeof(ui_rudder_gauge_t));
    if (gauge == NULL) {
        return NULL;
    }

    /* +/-45 deg covers the stops of a typical rudder; the sensor's own
     * plausible band is wider (see rudder_sensor.h) and the gauge clamps. */
    ui_rudder_gauge_create(gauge, parent, size, 45);
    ui_rudder_gauge_set_title(gauge, title);
    lv_obj_set_user_data(gauge->cont, gauge);
    lv_obj_add_event_cb(gauge->cont, gauge_free_cb, LV_EVENT_DELETE, gauge);
    return gauge->cont;
}

static void gauge_update(lv_obj_t *tile, const n2k_reading_t *reading)
{
    ui_rudder_gauge_t *gauge = lv_obj_get_user_data(tile);

    if (reading->valid) {
        ui_rudder_gauge_set_value(gauge, (int32_t)lroundf(reading->value));
    } else {
        ui_rudder_gauge_set_no_data(gauge);
    }
}

const ui_gauge_class_t ui_gauge_rudder = {
    .create = gauge_create,
    .update = gauge_update,
};
