#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Rudder angle sensor driver for a frequency-output rudder feedback unit
 * (Simrad RF300 and compatibles).
 *
 * The RF300 is a two-wire sensor: it is powered over the same pair it signals
 * on and reports the rudder angle by modulating its supply current into a
 * square wave. 3400 Hz is amidships and the frequency moves 20 Hz per degree.
 * The current modulation is squared up into a logic-level signal off-chip
 * (on this board, by the RS485 transceiver's receiver acting as a differential
 * comparator) and fed to the capture GPIO.
 *
 * Frequency is recovered by reciprocal counting: MCPWM timestamps rising edges
 * in hardware, and each measurement divides the number of elapsed input periods
 * by the time they spanned. That puts the +/-1 tick quantization error across a
 * whole window rather than a single period -- at an 80 MHz capture clock a 10 ms
 * window resolves far below 0.01 degrees, so the real limit is sensor jitter,
 * not the counter.
 *
 * There is no task and no queue. The capture ISR publishes each completed
 * window; callers poll rudder_sensor_read() whenever they need a value (e.g.
 * from an LVGL timer, or from the N2K transmit path for PGN 127245).
 */

typedef struct {
    /**
     * GPIO carrying the squared-up signal.
     *
     * On the Waveshare ESP32-S3-Touch-LCD-4.3B this is the RS485 receiver
     * output. The RGB panel, touch I2C, CAN and TF card leave almost no free
     * pins, so the RS485 port is repurposed as the sensor input.
     */
    int gpio_num;

    /**
     * Held high so an auto-direction RS485 transceiver keeps its driver off.
     *
     * If the transceiver's DI line is allowed to float or glitch low, the
     * driver turns on and fights the signal being injected onto A/B. Set to -1
     * when the capture pin is not behind an RS485 transceiver.
     */
    int rs485_tx_idle_gpio;

    int mcpwm_group_id;

    /**
     * Input periods per capture event. 1 disables prescaling.
     *
     * Raise it (to an even value) to cut the ISR rate, which at 3400 Hz is
     * otherwise one interrupt per period. Prescaling does not cost resolution
     * -- the window still spans the same number of input periods.
     */
    uint32_t prescale;

    float center_hz;     /* Frequency at amidships, 3400.0 for the RF300 */
    float hz_per_deg;    /* Frequency change per degree, 20.0 for the RF300 */
    float max_angle_deg; /* Mechanical full-scale; sets the plausible frequency band */

    /** Flip if the sensor reads starboard where the UI expects port. */
    bool invert;

    /**
     * Measurement window. Longer averages more sensor jitter, at the cost of
     * update rate. 10 ms gives a 100 Hz update, comfortably above the 10 Hz
     * that PGN 127245 is normally sent at.
     */
    uint32_t window_us;

    /** A reading older than this is reported as no-data rather than stale. */
    uint32_t timeout_ms;
} rudder_sensor_config_t;

/** Defaults for an RF300 on the 4.3B's RS485 port. */
#define RUDDER_SENSOR_DEFAULT_CONFIG()      \
    (rudder_sensor_config_t)                \
    {                                       \
        .gpio_num = 43,                     \
        .rs485_tx_idle_gpio = 44,           \
        .mcpwm_group_id = 0,                \
        .prescale = 1,                      \
        .center_hz = 3400.0f,               \
        .hz_per_deg = 20.0f,                \
        .max_angle_deg = 45.0f,             \
        .invert = false,                    \
        .window_us = 10000,                 \
        .timeout_ms = 250,                  \
    }

typedef struct {
    /**
     * False when there is no usable reading: nothing captured yet, the signal
     * stopped, or the frequency landed outside the plausible band. The other
     * fields are meaningless in that case -- show the no-data state rather
     * than a stale angle.
     */
    bool valid;
    float angle_deg;    /* Negative is port, positive starboard */
    float frequency_hz; /* Raw measured frequency, useful for diagnostics */
    int64_t timestamp_us; /* esp_timer time the window completed */
} rudder_sensor_reading_t;

/**
 * Claim the capture GPIO and MCPWM capture unit and start measuring.
 *
 * @param config  configuration; copied, so it need not outlive the call
 */
esp_err_t rudder_sensor_init(const rudder_sensor_config_t *config);

/**
 * Fetch the most recent measurement. Safe to call from any task.
 *
 * Returns ESP_OK with @p out->valid set false when there is no usable reading;
 * a missing sensor is a normal state, not an error.
 */
esp_err_t rudder_sensor_read(rudder_sensor_reading_t *out);

/** Stop capturing and release the GPIO and MCPWM resources. */
esp_err_t rudder_sensor_deinit(void);

#ifdef __cplusplus
}
#endif
