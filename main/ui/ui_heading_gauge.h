#pragma once

#include "lvgl.h"
#include "ui_gauge.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Vessel heading indicator built on the LVGL circular scale widget.
 *
 * A full-circle compass rose with north at the top and the cardinal points
 * labelled, a needle, and a numeric readout in the three-digit form headings
 * are conventionally written in (007, 127, 359).
 *
 * NMEA 2000 heading (PGN 127250) can be referenced to true or to magnetic
 * north, and the two differ by the local variation -- enough to matter. The
 * gauge therefore always states which one it is showing, in its own label
 * rather than folded into the title, since the reference can change if the
 * heading starts coming from a different device.
 *
 * All functions must be called with the LVGL lock held (see lvgl_port_lock()).
 */
typedef struct {
    lv_obj_t *cont;         /* Root container; use it to place/size the gauge */
    lv_obj_t *scale;        /* lv_scale showing the compass rose */
    lv_obj_t *needle;       /* lv_line acting as the needle */
    lv_obj_t *value_label;  /* Large numeric readout */
    lv_obj_t *ref_label;    /* "MAGNETIC" / "TRUE" reference indicator */
    lv_obj_t *title_label;  /* Caption at the bottom */
    int32_t value_deg;      /* Last value set, 0..359; meaningless when !valid */
    bool magnetic;          /* Reference of the last value set */
    bool valid;             /* False when no recent heading has been received */
} ui_heading_gauge_t;

/**
 * Create the gauge inside @p parent.
 *
 * @param gauge   caller-owned struct; must stay alive as long as the widget
 * @param parent  parent object, e.g. lv_screen_active()
 * @param size    width and height of the (square) gauge in pixels
 */
void ui_heading_gauge_create(ui_heading_gauge_t *gauge, lv_obj_t *parent, int32_t size);

/** Set the caption shown at the bottom (copied into the label). */
void ui_heading_gauge_set_title(ui_heading_gauge_t *gauge, const char *title);

/**
 * Show a new heading.
 *
 * @param heading_deg  0..359; values outside are wrapped, not clamped
 * @param magnetic     true for a magnetic reference, false for true north
 */
void ui_heading_gauge_set_value(ui_heading_gauge_t *gauge, int32_t heading_deg, bool magnetic);

/** Show the "no reading" state: needle parked on north, readout dashed out. */
void ui_heading_gauge_set_no_data(ui_heading_gauge_t *gauge);

/** Gauge class for the panel's tile machinery; see ui_gauge.h. */
extern const ui_gauge_class_t ui_gauge_compass;

#ifdef __cplusplus
}
#endif
