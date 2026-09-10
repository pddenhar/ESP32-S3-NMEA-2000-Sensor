#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build the application screen on the active display.
 * Must be called with the LVGL lock held.
 */
void ui_main_create(void);

/**
 * Push a new rudder angle into the UI (negative = port, positive = starboard).
 * Call this from the sensor task once GPIO reading is wired up.
 * Must be called with the LVGL lock held.
 */
void ui_main_set_rudder_angle(int32_t angle_deg);

/** Mark the rudder reading as unavailable (sensor fault, no data yet). */
void ui_main_set_rudder_no_data(void);

#ifdef __cplusplus
}
#endif
