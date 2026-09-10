#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reusable rudder angle indicator built on the LVGL circular scale widget.
 *
 * The gauge draws a half-circle scale spanning port (left, negative angles) to
 * starboard (right, positive angles) with a needle and a numeric readout.
 * Angles are whole degrees; 0 is rudder amidships.
 *
 * All functions must be called with the LVGL lock held (see lvgl_port_lock()).
 */
typedef struct {
    lv_obj_t *cont;         /* Root container; use it to place/size the gauge */
    lv_obj_t *scale;        /* lv_scale showing the tick marks and labels */
    lv_obj_t *needle;       /* lv_line acting as the needle */
    lv_obj_t *value_label;  /* Large numeric readout in the middle */
    lv_obj_t *title_label;  /* Caption above the readout */
    lv_scale_section_t *port_section;
    lv_scale_section_t *stbd_section;
    int32_t range_deg;      /* Full-scale deflection, e.g. 45 for +/-45 deg */
    int32_t value_deg;      /* Last value set; meaningless when !valid */
    bool valid;             /* False when the sensor has no usable reading */
    char tick_labels[7][8]; /* Backing store for the scale's label strings */
    const char *tick_label_ptrs[8];
} ui_rudder_gauge_t;

/**
 * Create the gauge inside @p parent.
 *
 * @param gauge      caller-owned struct; must stay alive as long as the widget
 * @param parent     parent object, e.g. lv_screen_active()
 * @param size       width and height of the (square) gauge in pixels
 * @param range_deg  full-scale deflection in degrees, e.g. 45 for +/-45 deg
 */
void ui_rudder_gauge_create(ui_rudder_gauge_t *gauge, lv_obj_t *parent, int32_t size, int32_t range_deg);

/** Set the caption shown under the readout (copied into the label). */
void ui_rudder_gauge_set_title(ui_rudder_gauge_t *gauge, const char *title);

/**
 * Show a new rudder angle. Negative is port, positive is starboard.
 * Values outside +/-range_deg are clamped so the needle stays on the arc.
 */
void ui_rudder_gauge_set_value(ui_rudder_gauge_t *gauge, int32_t angle_deg);

/** Show the "no reading" state: needle parked amidships, readout dashed out. */
void ui_rudder_gauge_set_no_data(ui_rudder_gauge_t *gauge);

#ifdef __cplusplus
}
#endif
