#pragma once

#include <stdint.h>

#include "lvgl.h"
#include "ui_gauge.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A plain scalar dial: a 270-degree scale, a needle, and a numeric readout with
 * its units.
 *
 * Unlike the rudder and compass gauges, which draw one quantity each and know
 * what it means, this one is told its range and units and will draw anything
 * that is a number between two bounds. Most of what a bus carries -- engine
 * temperature, oil pressure, depth, voltage -- needs nothing more, so a new
 * reading of that kind is a spec and a table row rather than a new widget.
 */

/** No warning band on that side of the range. */
#define UI_DIAL_NO_WARN INT32_MIN

#define UI_DIAL_MAX_MAJOR_TICKS 7

typedef struct {
    int32_t min;
    int32_t max;
    const char *units;    /* Drawn after the value, e.g. "psi" */
    uint8_t major_ticks;  /* Labelled ticks, at most UI_DIAL_MAX_MAJOR_TICKS */

    /**
     * Bands drawn in red, and the thresholds at which the readout turns red.
     * Which end is dangerous depends on the quantity: an engine overheats
     * above a temperature but loses oil pressure below one. UI_DIAL_NO_WARN
     * for an end that has no limit.
     */
    int32_t warn_below;
    int32_t warn_above;
} ui_dial_spec_t;

typedef struct {
    lv_obj_t *cont;
    lv_obj_t *scale;
    lv_obj_t *needle;
    lv_obj_t *value_label;
    lv_obj_t *title_label;
    const ui_dial_spec_t *spec; /* Must outlive the gauge; expected to be static */
    int32_t value;
    bool valid;
    char tick_labels[UI_DIAL_MAX_MAJOR_TICKS][8];
    const char *tick_label_ptrs[UI_DIAL_MAX_MAJOR_TICKS + 1];
} ui_dial_gauge_t;

void ui_dial_gauge_create(ui_dial_gauge_t *gauge, lv_obj_t *parent, int32_t size,
                          const ui_dial_spec_t *spec);

/** Set the caption shown under the readout (copied into the label). */
void ui_dial_gauge_set_title(ui_dial_gauge_t *gauge, const char *title);

/** Show a value. Outside the spec's range the needle stops at the end. */
void ui_dial_gauge_set_value(ui_dial_gauge_t *gauge, int32_t value);

/** Show the "no reading" state: needle parked at the low end, readout dashed. */
void ui_dial_gauge_set_no_data(ui_dial_gauge_t *gauge);

/* Shared by the generated classes below; not useful on their own. */
lv_obj_t *ui_dial_gauge_class_create(lv_obj_t *parent, int32_t size, const char *title,
                                     const ui_dial_spec_t *spec);
void ui_dial_gauge_class_update(lv_obj_t *tile, const n2k_reading_t *reading);

/**
 * Define a gauge class bound to one spec.
 *
 * The gauge class interface carries no room for per-channel configuration by
 * design -- ui_main should not have to know what a gauge needs -- so each dial
 * instrument gets its own tiny class that supplies its own spec.
 */
#define UI_DIAL_GAUGE_CLASS_DEFINE(class_name, spec_ptr)                          \
    static lv_obj_t *class_name##_create(lv_obj_t *parent, int32_t size,          \
                                         const char *title)                       \
    {                                                                             \
        return ui_dial_gauge_class_create(parent, size, title, (spec_ptr));       \
    }                                                                             \
    const ui_gauge_class_t class_name = {                                         \
        .create = class_name##_create,                                            \
        .update = ui_dial_gauge_class_update,                                     \
    }

#ifdef __cplusplus
}
#endif
