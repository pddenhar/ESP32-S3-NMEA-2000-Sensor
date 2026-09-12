#include <math.h>
#include <stdio.h>

#include "ui_heading_gauge.h"

/* A full circle with the eight cardinal and intercardinal points labelled.
 * LVGL spreads the ticks across both ends of the range, so the 360 deg tick
 * lands on top of the 0 deg one -- hence nine labels, with north repeated. */
#define GAUGE_ANGLE_RANGE   360
/* LVGL measures from 3 o'clock, clockwise; 270 puts 0 deg (north) at the top. */
#define GAUGE_ROTATION      270
#define GAUGE_MAJOR_TICKS   9
#define GAUGE_TICKS_PER_MAJOR 5
#define GAUGE_TOTAL_TICKS   ((GAUGE_MAJOR_TICKS - 1) * GAUGE_TICKS_PER_MAJOR + 1)

/* The dial is kept clear of the bottom of the container so the readout sits
 * under it rather than behind the needle, which is drawn from the centre out. */
#define GAUGE_DIAL_INSET    100

#define COLOR_BG            lv_color_hex(0x1B1F25)
#define COLOR_FG            lv_color_hex(0xE8ECF0)
#define COLOR_DIM           lv_color_hex(0x8A939E)
#define COLOR_NORTH         lv_color_hex(0xE03030)
/* Magnetic and true get different colours as well as different words, so the
 * reference can be told apart at a glance from across the cockpit. */
#define COLOR_MAGNETIC      lv_color_hex(0xE0A030)
#define COLOR_TRUE          lv_color_hex(0x4FA8FF)

/* Referenced by the scale for its lifetime, so it outlives any one gauge. */
static const char *s_tick_labels[GAUGE_MAJOR_TICKS + 1] = {
    "N", "NE", "E", "SE", "S", "SW", "W", "NW", "N", NULL,
};

/* The north sector is styled separately so the top of the rose reads as north
 * without having to find the label. */
static lv_style_t s_north_items;
static lv_style_t s_north_main;
static bool s_styles_ready;

static void styles_init(void)
{
    if (s_styles_ready) {
        return;
    }

    lv_style_init(&s_north_items);
    lv_style_set_line_color(&s_north_items, COLOR_NORTH);
    lv_style_set_line_width(&s_north_items, 4);
    lv_style_set_text_color(&s_north_items, COLOR_NORTH);

    lv_style_init(&s_north_main);
    lv_style_set_arc_color(&s_north_main, COLOR_NORTH);
    lv_style_set_arc_width(&s_north_main, 5);

    s_styles_ready = true;
}

static void update_readout(ui_heading_gauge_t *gauge)
{
    if (!gauge->valid) {
        lv_label_set_text(gauge->value_label, "---\xC2\xB0");
        lv_obj_set_style_text_color(gauge->value_label, COLOR_DIM, 0);
        lv_label_set_text(gauge->ref_label, "NO DATA");
        lv_obj_set_style_text_color(gauge->ref_label, COLOR_DIM, 0);
        lv_obj_set_style_line_color(gauge->needle, COLOR_DIM, 0);
        lv_scale_set_line_needle_value(gauge->scale, gauge->needle, -20, 0);
        return;
    }

    /* Three digits with leading zeros: the way a heading is written and read
     * aloud, and it keeps the readout from changing width as it swings. */
    lv_label_set_text_fmt(gauge->value_label, "%03d\xC2\xB0", (int)gauge->value_deg);
    lv_obj_set_style_text_color(gauge->value_label, COLOR_FG, 0);

    if (gauge->magnetic) {
        lv_label_set_text(gauge->ref_label, "MAGNETIC");
        lv_obj_set_style_text_color(gauge->ref_label, COLOR_MAGNETIC, 0);
        lv_obj_set_style_line_color(gauge->needle, COLOR_MAGNETIC, 0);
    } else {
        lv_label_set_text(gauge->ref_label, "TRUE");
        lv_obj_set_style_text_color(gauge->ref_label, COLOR_TRUE, 0);
        lv_obj_set_style_line_color(gauge->needle, COLOR_TRUE, 0);
    }

    lv_scale_set_line_needle_value(gauge->scale, gauge->needle, -20, gauge->value_deg);
}

void ui_heading_gauge_create(ui_heading_gauge_t *gauge, lv_obj_t *parent, int32_t size)
{
    styles_init();

    lv_memzero(gauge, sizeof(*gauge));
    gauge->valid = false;

    gauge->cont = lv_obj_create(parent);
    lv_obj_set_size(gauge->cont, size, size);
    lv_obj_remove_flag(gauge->cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(gauge->cont, COLOR_BG, 0);
    lv_obj_set_style_border_color(gauge->cont, lv_color_hex(0x2E353E), 0);
    lv_obj_set_style_border_width(gauge->cont, 2, 0);
    lv_obj_set_style_radius(gauge->cont, 12, 0);
    lv_obj_set_style_pad_all(gauge->cont, 8, 0);

    gauge->scale = lv_scale_create(gauge->cont);
    lv_obj_set_size(gauge->scale, size - GAUGE_DIAL_INSET, size - GAUGE_DIAL_INSET);
    lv_obj_align(gauge->scale, LV_ALIGN_TOP_MID, 0, 0);
    lv_scale_set_mode(gauge->scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_angle_range(gauge->scale, GAUGE_ANGLE_RANGE);
    lv_scale_set_rotation(gauge->scale, GAUGE_ROTATION);
    lv_scale_set_total_tick_count(gauge->scale, GAUGE_TOTAL_TICKS);
    lv_scale_set_major_tick_every(gauge->scale, GAUGE_TICKS_PER_MAJOR);
    lv_scale_set_range(gauge->scale, 0, 360);
    lv_scale_set_label_show(gauge->scale, true);
    lv_scale_set_text_src(gauge->scale, s_tick_labels);

    lv_obj_set_style_bg_opa(gauge->scale, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gauge->scale, 0, 0);
    lv_obj_set_style_pad_all(gauge->scale, 4, 0);
    lv_obj_set_style_text_color(gauge->scale, COLOR_FG, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(gauge->scale, &lv_font_montserrat_14, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(gauge->scale, COLOR_FG, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(gauge->scale, 3, LV_PART_INDICATOR);
    lv_obj_set_style_length(gauge->scale, 12, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(gauge->scale, COLOR_DIM, LV_PART_ITEMS);
    lv_obj_set_style_line_width(gauge->scale, 2, LV_PART_ITEMS);
    lv_obj_set_style_length(gauge->scale, 6, LV_PART_ITEMS);
    lv_obj_set_style_arc_color(gauge->scale, COLOR_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gauge->scale, 3, LV_PART_MAIN);

    /* One 45 deg sector either side of north, so the marked arc is centred on
     * the top of the dial. */
    lv_scale_section_t *north = lv_scale_add_section(gauge->scale);
    lv_scale_set_section_range(gauge->scale, north, 0, 45);
    lv_scale_set_section_style_items(gauge->scale, north, &s_north_items);
    lv_scale_set_section_style_main(gauge->scale, north, &s_north_main);

    lv_scale_section_t *north_west = lv_scale_add_section(gauge->scale);
    lv_scale_set_section_range(gauge->scale, north_west, 315, 360);
    lv_scale_set_section_style_items(gauge->scale, north_west, &s_north_items);
    lv_scale_set_section_style_main(gauge->scale, north_west, &s_north_main);

    gauge->needle = lv_line_create(gauge->scale);
    lv_obj_set_style_line_width(gauge->needle, 6, 0);
    lv_obj_set_style_line_rounded(gauge->needle, true, 0);

    gauge->value_label = lv_label_create(gauge->cont);
    lv_obj_set_style_text_font(gauge->value_label, &lv_font_montserrat_28, 0);
    lv_obj_align(gauge->value_label, LV_ALIGN_BOTTOM_MID, 0, -48);

    gauge->ref_label = lv_label_create(gauge->cont);
    lv_obj_set_style_text_font(gauge->ref_label, &lv_font_montserrat_14, 0);
    lv_obj_align(gauge->ref_label, LV_ALIGN_BOTTOM_MID, 0, -26);

    gauge->title_label = lv_label_create(gauge->cont);
    lv_obj_set_style_text_font(gauge->title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gauge->title_label, COLOR_DIM, 0);
    lv_label_set_text(gauge->title_label, "HEADING");
    lv_obj_align(gauge->title_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    update_readout(gauge);
}

void ui_heading_gauge_set_title(ui_heading_gauge_t *gauge, const char *title)
{
    lv_label_set_text(gauge->title_label, title);
}

void ui_heading_gauge_set_value(ui_heading_gauge_t *gauge, int32_t heading_deg, bool magnetic)
{
    heading_deg %= 360;
    if (heading_deg < 0) {
        heading_deg += 360;
    }

    if (gauge->valid && gauge->value_deg == heading_deg && gauge->magnetic == magnetic) {
        return;
    }

    gauge->value_deg = heading_deg;
    gauge->magnetic = magnetic;
    gauge->valid = true;
    update_readout(gauge);
}

void ui_heading_gauge_set_no_data(ui_heading_gauge_t *gauge)
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

static lv_obj_t *gauge_create(lv_obj_t *parent, int32_t size, const char *title)
{
    ui_heading_gauge_t *gauge = lv_malloc(sizeof(ui_heading_gauge_t));
    if (gauge == NULL) {
        return NULL;
    }

    ui_heading_gauge_create(gauge, parent, size);
    ui_heading_gauge_set_title(gauge, title);
    lv_obj_set_user_data(gauge->cont, gauge);
    lv_obj_add_event_cb(gauge->cont, gauge_free_cb, LV_EVENT_DELETE, gauge);
    return gauge->cont;
}

static void gauge_update(lv_obj_t *tile, const n2k_reading_t *reading)
{
    ui_heading_gauge_t *gauge = lv_obj_get_user_data(tile);

    if (reading->valid) {
        ui_heading_gauge_set_value(gauge, (int32_t)lroundf(reading->value),
                                   reading->qualifier == N2K_HEADING_REF_MAGNETIC);
    } else {
        ui_heading_gauge_set_no_data(gauge);
    }
}

const ui_gauge_class_t ui_gauge_compass = {
    .create = gauge_create,
    .update = gauge_update,
};
