#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The set of values this display can show, one per PGN.
 *
 * Everything on the panel is a channel, whether the value arrives from the
 * NMEA 2000 bus or is measured by this device: rudder angle is not a special
 * case, it is simply the channel whose data happens to originate here. That
 * makes the display side uniform -- read a channel, hand the reading to a gauge
 * -- and it is what keeps the cost of adding a PGN flat.
 *
 * To add one: add an id below, a row to the table in n2k_channels.c, a case in
 * the bridge's receive handler that publishes into it, and (when there is a
 * widget for it) a gauge class in ui_main.c.
 */
typedef enum {
    N2K_CH_RUDDER = 0,          /* PGN 127245, measured locally */
    N2K_CH_HEADING,             /* PGN 127250, received from the bus */
    N2K_CH_ENGINE_SPEED,        /* PGN 127488, crankshaft RPM */
    N2K_CH_ENGINE_TEMP,         /* PGN 127489, coolant temperature */
    N2K_CH_ENGINE_OIL_PRESSURE, /* PGN 127489, oil pressure */
    N2K_CH_COUNT,
} n2k_channel_id_t;

/**
 * Engine readouts in US customary units (deg F, psi) rather than SI (deg C,
 * kPa). Set to 0 for SI.
 *
 * This is the one switch; the unit labels and conversions below follow from
 * it, and so do the dial ranges and alarm points in ui_engine_gauges.c. Both
 * systems use units that read as whole numbers -- kPa rather than bar --
 * because the dials draw integers.
 */
#define N2K_ENGINE_UNITS_US 1

#if N2K_ENGINE_UNITS_US
#define N2K_TEMP_UNITS "\xC2\xB0" "F"
#define N2K_PRESSURE_UNITS "psi"
#else
#define N2K_TEMP_UNITS "\xC2\xB0" "C"
#define N2K_PRESSURE_UNITS "kPa"
#endif

/** Engine speed needs no unit system: RPM is RPM. */
#define N2K_RPM_UNITS "rpm"

/** NMEA 2000 carries temperature in kelvin and pressure in pascal. */
static inline float n2k_temp_from_kelvin(double kelvin)
{
#if N2K_ENGINE_UNITS_US
    return (float)((kelvin - 273.15) * 9.0 / 5.0 + 32.0);
#else
    return (float)(kelvin - 273.15);
#endif
}

static inline float n2k_pressure_from_pascal(double pascal)
{
#if N2K_ENGINE_UNITS_US
    return (float)(pascal / 6894.757);
#else
    return (float)(pascal / 1000.0);
#endif
}

/** Source address used for channels measured by this device. */
#define N2K_SOURCE_LOCAL 0xFF

/** Reference frame a heading is measured against; the qualifier for N2K_CH_HEADING. */
typedef enum {
    N2K_HEADING_REF_TRUE = 0,
    N2K_HEADING_REF_MAGNETIC,
} n2k_heading_ref_t;

typedef struct {
    /**
     * False when there is no usable value: nothing received or measured yet,
     * the sender stopped, or the reading was reported as not available. The
     * other fields are meaningless in that case -- show the no-data state
     * rather than a stale value.
     */
    bool valid;
    float value;             /* Primary scalar, in the channel's display units */
    uint8_t qualifier;       /* Channel-specific; see the id's comment above */
    uint8_t source_address;  /* Device that sent it, or N2K_SOURCE_LOCAL */
    int64_t timestamp_us;    /* esp_timer time the value was produced */
} n2k_reading_t;

/** Longest channel key, including the terminator. Kept short: it goes in NVS. */
#define N2K_CHANNEL_KEY_MAX 16

typedef struct {
    /**
     * Stable identifier, used to remember a channel across reboots and
     * firmware updates. Never reuse or rename a key once shipped, or a saved
     * selection turns into a different gauge.
     *
     * A PGN will not do for this: one message can feed several channels --
     * 127489 carries coolant temperature and oil pressure both -- so the PGN
     * does not identify which of them was chosen.
     */
    const char *key;

    unsigned long pgn;   /* Several channels may share one */
    const char *name;    /* "Rudder Angle": menu row and tile caption */
    const char *units;   /* Short unit label, "deg"; NULL when the gauge says it */
    uint32_t timeout_ms; /* A value older than this is reported as no-data */

    /**
     * Non-NULL for channels measured by this device, in which case the value is
     * fetched on demand rather than cached: the sensor driver is already the
     * authoritative store, and going through it keeps the display's update rate
     * independent of anything else.
     */
    bool (*pull)(n2k_reading_t *out);
} n2k_channel_desc_t;

/**
 * Publish a received value. Called from the NMEA 2000 task.
 *
 * When several devices send the same PGN, the first one heard from keeps the
 * channel until it goes quiet for longer than the channel's timeout; publishes
 * from other sources are dropped meanwhile. Without that the readout alternates
 * between two senders that disagree slightly.
 */
void n2k_channels_publish(n2k_channel_id_t id, const n2k_reading_t *reading);

/**
 * Fetch the current value of a channel. Safe to call from any task.
 *
 * Returns ESP_OK with @p out->valid false when there is nothing recent; a
 * channel with no sender is a normal state, not an error.
 */
esp_err_t n2k_channels_read(n2k_channel_id_t id, n2k_reading_t *out);

/** Static description of a channel. NULL if @p id is out of range. */
const n2k_channel_desc_t *n2k_channels_desc(n2k_channel_id_t id);

/**
 * Channel with this key, or -1 if this firmware no longer has one.
 *
 * There is deliberately no lookup by PGN: several channels can share one, so
 * the answer would be ambiguous exactly where it matters.
 */
int n2k_channels_find_by_key(const char *key);

#ifdef __cplusplus
}
#endif
