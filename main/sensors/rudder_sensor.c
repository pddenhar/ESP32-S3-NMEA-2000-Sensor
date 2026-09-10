#include "rudder_sensor.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/mcpwm_cap.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "rudder";

/* Requested capture tick rate. The driver may not honour it exactly, so the
 * achieved rate is read back and all timing is derived from that instead. */
#define RUDDER_CAP_RESOLUTION_HZ (80 * 1000 * 1000)

/* How far past full-scale deflection a reading is still accepted. The sensor
 * can be driven slightly beyond its rated angle by rudder over-travel; the UI
 * clamps for display, so rejecting those outright would blank the gauge at
 * exactly the moment it matters most. */
#define RUDDER_BAND_MARGIN 1.15f

/* A gap longer than this many times the slowest plausible capture interval
 * means the signal dropped out, so the window in progress is abandoned. */
#define RUDDER_GAP_FACTOR 4

typedef struct {
    mcpwm_cap_timer_handle_t cap_timer;
    mcpwm_cap_channel_handle_t cap_chan;
    rudder_sensor_config_t cfg;

    uint32_t resolution_hz;    /* Actual capture tick rate */
    uint32_t window_ticks;     /* Minimum span before a window is published */
    uint32_t gap_reset_ticks;  /* Capture spacing that counts as a dropout */
    float min_hz;
    float max_hz;

    /* Window accumulator. Touched only by the capture ISR. */
    uint32_t win_first;   /* Timestamp of the edge the window opened on */
    uint32_t win_last;    /* Timestamp of the most recent edge */
    uint32_t win_count;   /* Capture intervals accumulated since win_first */
    bool win_open;

    /* Last completed window, shared with readers.
     *
     * Published as integers: the Xtensa FPU is not saved across interrupts, so
     * the ISR must not touch float. Readers do the division. */
    portMUX_TYPE lock;
    uint32_t pub_periods;    /* Input periods spanned */
    uint32_t pub_span_ticks; /* Capture ticks they took */
    int64_t pub_time_us;     /* 0 until the first window completes */
} rudder_ctx_t;

static rudder_ctx_t *s_ctx;

static bool IRAM_ATTR rudder_capture_cb(mcpwm_cap_channel_handle_t chan,
                                        const mcpwm_capture_event_data_t *edata,
                                        void *user_ctx)
{
    rudder_ctx_t *ctx = (rudder_ctx_t *)user_ctx;
    const uint32_t ts = edata->cap_value;

    if (!ctx->win_open) {
        ctx->win_first = ts;
        ctx->win_last = ts;
        ctx->win_count = 0;
        ctx->win_open = true;
        return false;
    }

    /* Unsigned arithmetic throughout: the capture counter is 32 bits and wraps
     * every ~54 s at 80 MHz, which these differences absorb correctly so long
     * as the window stays far shorter than that. */
    const uint32_t gap = ts - ctx->win_last;
    if (gap > ctx->gap_reset_ticks) {
        /* Signal stopped and came back. The interval spanning the gap would
         * read as an absurdly low frequency, so reopen the window here. */
        ctx->win_first = ts;
        ctx->win_last = ts;
        ctx->win_count = 0;
        return false;
    }

    ctx->win_last = ts;
    ctx->win_count++;

    const uint32_t span = ts - ctx->win_first;
    if (span >= ctx->window_ticks) {
        portENTER_CRITICAL_ISR(&ctx->lock);
        ctx->pub_periods = ctx->win_count * ctx->cfg.prescale;
        ctx->pub_span_ticks = span;
        ctx->pub_time_us = esp_timer_get_time();
        portEXIT_CRITICAL_ISR(&ctx->lock);

        /* Reopen on this edge so no input periods are dropped between windows. */
        ctx->win_first = ts;
        ctx->win_count = 0;
    }

    return false;
}

esp_err_t rudder_sensor_init(const rudder_sensor_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ctx != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config->hz_per_deg <= 0.0f || config->max_angle_deg <= 0.0f || config->window_us == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* The capture prescaler bypasses at 0 or 1 and otherwise requires an even
     * ratio; an odd value would be silently rounded and skew every reading. */
    if (config->prescale > 1 && (config->prescale % 2) != 0) {
        ESP_LOGE(TAG, "prescale must be 1 or an even number, got %" PRIu32, config->prescale);
        return ESP_ERR_INVALID_ARG;
    }

    rudder_ctx_t *ctx = calloc(1, sizeof(rudder_ctx_t));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->cfg = *config;
    if (ctx->cfg.prescale == 0) {
        ctx->cfg.prescale = 1;
    }
    ctx->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

    esp_err_t ret = ESP_OK;

    /* Park the RS485 driver before anything starts listening: an auto-direction
     * transceiver enables its driver whenever DI goes low, which would overwrite
     * the signal on A/B with the ESP32's own idle level.
     *
     * Held by a pull-up rather than driven, deliberately. DI is a high-impedance
     * CMOS input, so the internal pull-up holds it high comfortably -- and if
     * this pin turns out to be the transceiver's RO output instead (the pin
     * direction is not confirmed against the schematic), a pull-up is simply
     * overridden, where a push-pull output would put two drivers in contention. */
    if (ctx->cfg.rs485_tx_idle_gpio >= 0) {
        const gpio_config_t idle_conf = {
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pin_bit_mask = 1ULL << ctx->cfg.rs485_tx_idle_gpio,
        };
        ret = gpio_config(&idle_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "RS485 idle pin config failed: %s", esp_err_to_name(ret));
            goto fail;
        }
    }

    const mcpwm_capture_timer_config_t timer_conf = {
        .group_id = ctx->cfg.mcpwm_group_id,
        .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
        .resolution_hz = RUDDER_CAP_RESOLUTION_HZ,
    };
    ret = mcpwm_new_capture_timer(&timer_conf, &ctx->cap_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "capture timer alloc failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ret = mcpwm_capture_timer_get_resolution(ctx->cap_timer, &ctx->resolution_hz);
    if (ret != ESP_OK || ctx->resolution_hz == 0) {
        ESP_LOGE(TAG, "capture resolution unavailable: %s", esp_err_to_name(ret));
        ret = (ret == ESP_OK) ? ESP_ERR_INVALID_STATE : ret;
        goto fail;
    }

    /* Only rising edges: using both would fold the sensor's duty-cycle error
     * into every other interval. */
    const mcpwm_capture_channel_config_t chan_conf = {
        .gpio_num = ctx->cfg.gpio_num,
        .prescale = ctx->cfg.prescale,
        .flags.pos_edge = true,
        .flags.neg_edge = false,
    };
    ret = mcpwm_new_capture_channel(ctx->cap_timer, &chan_conf, &ctx->cap_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "capture channel alloc on GPIO%d failed: %s", ctx->cfg.gpio_num,
                 esp_err_to_name(ret));
        goto fail;
    }

    const mcpwm_capture_event_callbacks_t cbs = {
        .on_cap = rudder_capture_cb,
    };
    ret = mcpwm_capture_channel_register_event_callbacks(ctx->cap_chan, &cbs, ctx);
    if (ret != ESP_OK) {
        goto fail;
    }

    const float band = ctx->cfg.max_angle_deg * ctx->cfg.hz_per_deg * RUDDER_BAND_MARGIN;
    ctx->min_hz = ctx->cfg.center_hz - band;
    ctx->max_hz = ctx->cfg.center_hz + band;
    if (ctx->min_hz <= 0.0f) {
        ESP_LOGE(TAG, "sensor range spans DC; check center_hz and max_angle_deg");
        ret = ESP_ERR_INVALID_ARG;
        goto fail;
    }

    ctx->window_ticks = (uint32_t)((uint64_t)ctx->resolution_hz * ctx->cfg.window_us / 1000000ULL);
    ctx->gap_reset_ticks = (uint32_t)((float)ctx->resolution_hz * ctx->cfg.prescale *
                                      RUDDER_GAP_FACTOR / ctx->min_hz);

    ret = mcpwm_capture_channel_enable(ctx->cap_chan);
    if (ret != ESP_OK) {
        goto fail;
    }
    ret = mcpwm_capture_timer_enable(ctx->cap_timer);
    if (ret != ESP_OK) {
        goto fail;
    }
    ret = mcpwm_capture_timer_start(ctx->cap_timer);
    if (ret != ESP_OK) {
        goto fail;
    }

    s_ctx = ctx;
    ESP_LOGI(TAG, "capture on GPIO%d at %" PRIu32 " Hz, window %" PRIu32 " us, band %.0f-%.0f Hz",
             ctx->cfg.gpio_num, ctx->resolution_hz, ctx->cfg.window_us,
             (double)ctx->min_hz, (double)ctx->max_hz);
    return ESP_OK;

fail:
    if (ctx->cap_chan != NULL) {
        mcpwm_del_capture_channel(ctx->cap_chan);
    }
    if (ctx->cap_timer != NULL) {
        mcpwm_del_capture_timer(ctx->cap_timer);
    }
    free(ctx);
    return ret;
}

esp_err_t rudder_sensor_read(rudder_sensor_reading_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    rudder_ctx_t *ctx = s_ctx;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t periods;
    uint32_t span_ticks;
    int64_t time_us;
    portENTER_CRITICAL(&ctx->lock);
    periods = ctx->pub_periods;
    span_ticks = ctx->pub_span_ticks;
    time_us = ctx->pub_time_us;
    portEXIT_CRITICAL(&ctx->lock);

    memset(out, 0, sizeof(*out));
    out->timestamp_us = time_us;

    if (time_us == 0 || span_ticks == 0) {
        return ESP_OK; /* Nothing captured yet */
    }

    const float freq = (float)periods * (float)ctx->resolution_hz / (float)span_ticks;
    out->frequency_hz = freq;

    if (esp_timer_get_time() - time_us > (int64_t)ctx->cfg.timeout_ms * 1000) {
        return ESP_OK; /* Signal stopped; report no-data rather than a stale angle */
    }
    if (freq < ctx->min_hz || freq > ctx->max_hz) {
        return ESP_OK; /* Implausible: noise, a wiring fault, or a wrong prescale */
    }

    float angle = (freq - ctx->cfg.center_hz) / ctx->cfg.hz_per_deg;
    if (ctx->cfg.invert) {
        angle = -angle;
    }
    out->angle_deg = angle;
    out->valid = true;
    return ESP_OK;
}

esp_err_t rudder_sensor_deinit(void)
{
    rudder_ctx_t *ctx = s_ctx;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_ctx = NULL;

    ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_capture_channel_disable(ctx->cap_chan));
    ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_del_capture_channel(ctx->cap_chan));
    ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_capture_timer_disable(ctx->cap_timer));
    ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_del_capture_timer(ctx->cap_timer));
    if (ctx->cfg.rs485_tx_idle_gpio >= 0) {
        gpio_reset_pin(ctx->cfg.rs485_tx_idle_gpio);
    }
    gpio_reset_pin(ctx->cfg.gpio_num);

    free(ctx);
    return ESP_OK;
}
