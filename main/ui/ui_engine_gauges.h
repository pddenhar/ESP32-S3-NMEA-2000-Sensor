#pragma once

#include "ui_gauge.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Engine instruments, all drawn by the generic dial in ui_dial_gauge.h.
 *
 * Each is its own gauge class because each is its own channel: they are
 * chosen, shown and blanked independently, whether they come from separate
 * messages (speed, from PGN 127488) or share one (temperature and oil
 * pressure, both from PGN 127489).
 */
extern const ui_gauge_class_t ui_gauge_engine_speed;
extern const ui_gauge_class_t ui_gauge_engine_temp;
extern const ui_gauge_class_t ui_gauge_oil_pressure;

#ifdef __cplusplus
}
#endif
