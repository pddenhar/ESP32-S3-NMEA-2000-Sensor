#include "ui_engine_gauges.h"
#include "ui_dial_gauge.h"
#include "n2k_channels.h"

/* Dial ranges and alarm points. They belong here rather than beside the channel
 * table because they are decisions about drawing, not about the data -- but
 * they follow N2K_ENGINE_UNITS_US, so that the dial, the readout and the
 * channel's unit label cannot disagree about which system is in use. */
#if N2K_ENGINE_UNITS_US
#define TEMP_MIN 20
#define TEMP_MAX 260
#define TEMP_WARN_ABOVE 210
#define PRESSURE_MIN 0
#define PRESSURE_MAX 120
#define PRESSURE_WARN_BELOW 15
#else
#define TEMP_MIN 0
#define TEMP_MAX 120
#define TEMP_WARN_ABOVE 99
#define PRESSURE_MIN 0
#define PRESSURE_MAX 600
#define PRESSURE_WARN_BELOW 100
#endif

/* 0-6000 in thousands suits a range of marine engines. The redline does not:
 * it belongs to one particular engine, so there is no alarm band by default.
 * Set RPM_WARN_ABOVE to the engine's own limit to get one -- a wrong redline
 * on a tachometer is worse than no redline at all. */
#define RPM_MIN 0
#define RPM_MAX 6000
#define RPM_WARN_ABOVE UI_DIAL_NO_WARN

static const ui_dial_spec_t s_engine_speed_spec = {
    .min = RPM_MIN,
    .max = RPM_MAX,
    .units = N2K_RPM_UNITS,
    .major_ticks = 7,
    .warn_below = UI_DIAL_NO_WARN,
    .warn_above = RPM_WARN_ABOVE,
};

static const ui_dial_spec_t s_engine_temp_spec = {
    .min = TEMP_MIN,
    .max = TEMP_MAX,
    .units = N2K_TEMP_UNITS,
    .major_ticks = 7,
    /* An engine is in trouble when it runs hot, not when it runs cold: a cold
     * reading means it has not warmed up yet, which is not an alarm. */
    .warn_below = UI_DIAL_NO_WARN,
    .warn_above = TEMP_WARN_ABOVE,
};

static const ui_dial_spec_t s_oil_pressure_spec = {
    .min = PRESSURE_MIN,
    .max = PRESSURE_MAX,
    .units = N2K_PRESSURE_UNITS,
    .major_ticks = 7,
    /* The opposite way round: losing oil pressure is what destroys an engine. */
    .warn_below = PRESSURE_WARN_BELOW,
    .warn_above = UI_DIAL_NO_WARN,
};

UI_DIAL_GAUGE_CLASS_DEFINE(ui_gauge_engine_speed, &s_engine_speed_spec);
UI_DIAL_GAUGE_CLASS_DEFINE(ui_gauge_engine_temp, &s_engine_temp_spec);
UI_DIAL_GAUGE_CLASS_DEFINE(ui_gauge_oil_pressure, &s_oil_pressure_spec);
