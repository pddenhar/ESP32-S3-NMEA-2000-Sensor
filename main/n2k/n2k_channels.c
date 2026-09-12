#include "n2k_channels.h"
#include "rudder_sensor.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

/* Set to 1 to sweep the rudder channel from a canned animation instead of the
 * sensor, for checking the panel without the rudder feedback unit connected.
 * It sits here rather than in the UI so the sweep drives the transmitted PGN
 * too, which is what makes it useful for testing a plotter at the other end of
 * the bus rather than just the layout. */
#define N2K_CH_RUDDER_DEMO 0

/* PGN 127250 is normally transmitted at 10 Hz, so three seconds of silence
 * means the compass has stopped rather than that we missed a message. */
#define N2K_HEADING_TIMEOUT_MS 3000

/* The rudder driver applies its own, much tighter, timeout before it reports a
 * reading as invalid; this is a backstop for the value having been fetched. */
#define N2K_RUDDER_TIMEOUT_MS 1000

/* PGN 127489 is normally transmitted at 2 Hz. */
#define N2K_ENGINE_TIMEOUT_MS 3000

/* PGN 127488 is a rapid-update message, sent at 10 Hz, so a shorter timeout
 * still allows plenty of missed messages while blanking a stopped feed
 * promptly -- which matters most for the reading that changes fastest. */
#define N2K_ENGINE_RAPID_TIMEOUT_MS 1000

static bool rudder_pull(n2k_reading_t *out)
{
#if N2K_CH_RUDDER_DEMO
    static float angle = 0.0f;
    static float step = 0.5f;

    angle += step;
    if (angle >= 35.0f || angle <= -35.0f) {
        step = -step;
    }

    out->valid = true;
    out->value = angle;
    out->qualifier = 0;
    out->source_address = N2K_SOURCE_LOCAL;
    out->timestamp_us = esp_timer_get_time();
    return true;
#else
    rudder_sensor_reading_t reading;
    if (rudder_sensor_read(&reading) != ESP_OK || !reading.valid) {
        return false;
    }

    out->valid = true;
    out->value = reading.angle_deg;
    out->qualifier = 0;
    out->source_address = N2K_SOURCE_LOCAL;
    out->timestamp_us = reading.timestamp_us;
    return true;
#endif
}

static const n2k_channel_desc_t s_channels[N2K_CH_COUNT] = {
    [N2K_CH_RUDDER] = {
        .key = "rudder",
        .pgn = 127245UL,
        .name = "Rudder Angle",
        .units = "deg",
        .timeout_ms = N2K_RUDDER_TIMEOUT_MS,
        .pull = rudder_pull,
    },
    [N2K_CH_HEADING] = {
        .key = "heading",
        .pgn = 127250UL,
        .name = "Vessel Heading",
        .units = "deg",
        .timeout_ms = N2K_HEADING_TIMEOUT_MS,
        .pull = NULL,
    },
    [N2K_CH_ENGINE_SPEED] = {
        .key = "engine_speed",
        .pgn = 127488UL,
        .name = "Engine Speed",
        .units = N2K_RPM_UNITS,
        .timeout_ms = N2K_ENGINE_RAPID_TIMEOUT_MS,
        .pull = NULL,
    },
    /* Two channels off one message: PGN 127489 carries a dozen engine
     * readings, and each one the panel can draw is its own channel. */
    [N2K_CH_ENGINE_TEMP] = {
        .key = "engine_temp",
        .pgn = 127489UL,
        .name = "Engine Coolant Temp",
        .units = N2K_TEMP_UNITS,
        .timeout_ms = N2K_ENGINE_TIMEOUT_MS,
        .pull = NULL,
    },
    [N2K_CH_ENGINE_OIL_PRESSURE] = {
        .key = "engine_oil_press",
        .pgn = 127489UL,
        .name = "Engine Oil Pressure",
        .units = N2K_PRESSURE_UNITS,
        .timeout_ms = N2K_ENGINE_TIMEOUT_MS,
        .pull = NULL,
    },
};

/* Written by the NMEA 2000 task, read by the UI task. The spinlock keeps a
 * reader from seeing half of an update; a reading is small enough that copying
 * it under the lock costs less than any double-buffering scheme. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static n2k_reading_t s_readings[N2K_CH_COUNT];

static bool reading_is_fresh(const n2k_reading_t *reading, uint32_t timeout_ms, int64_t now_us)
{
    return reading->valid && (now_us - reading->timestamp_us) <= (int64_t)timeout_ms * 1000;
}

void n2k_channels_publish(n2k_channel_id_t id, const n2k_reading_t *reading)
{
    if (id < 0 || id >= N2K_CH_COUNT || reading == NULL) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&s_lock);
    /* Hold the incumbent sender while it is still talking. Compared inside the
     * lock so two sources arriving back to back cannot both win. */
    const bool incumbent_active =
        reading_is_fresh(&s_readings[id], s_channels[id].timeout_ms, now_us) &&
        s_readings[id].source_address != reading->source_address;
    if (!incumbent_active) {
        s_readings[id] = *reading;
    }
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t n2k_channels_read(n2k_channel_id_t id, n2k_reading_t *out)
{
    if (id < 0 || id >= N2K_CH_COUNT || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Locally measured channels are fetched straight from their driver, which
     * is already the authoritative store. Done outside the lock: the driver has
     * its own synchronisation and there is no shared state to protect. */
    if (s_channels[id].pull != NULL) {
        if (!s_channels[id].pull(out) ||
            !reading_is_fresh(out, s_channels[id].timeout_ms, esp_timer_get_time())) {
            *out = (n2k_reading_t){0};
        }
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_lock);
    *out = s_readings[id];
    portEXIT_CRITICAL(&s_lock);

    /* Aged out here rather than in the publish path so a sender that stops
     * blanks the reading without needing anything to run. */
    if (!reading_is_fresh(out, s_channels[id].timeout_ms, esp_timer_get_time())) {
        out->valid = false;
    }
    return ESP_OK;
}

const n2k_channel_desc_t *n2k_channels_desc(n2k_channel_id_t id)
{
    if (id < 0 || id >= N2K_CH_COUNT) {
        return NULL;
    }
    return &s_channels[id];
}

int n2k_channels_find_by_key(const char *key)
{
    if (key == NULL) {
        return -1;
    }
    for (int i = 0; i < N2K_CH_COUNT; i++) {
        if (s_channels[i].key != NULL && strcmp(s_channels[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}
