/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/lvgl_port.h"
#include "bsp/board.h"

static const char *TAG = "lv_port";                      // Tag for logging

IRAM_ATTR static bool rgb_lcd_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata,
 void *user_ctx)
{
    return lvgl_port_notify_rgb_vsync();
}

/* Consecutive failed touch reads before the poll backs off. Small enough that a
 * genuine fault is caught quickly, large enough that a stray glitch is absorbed
 * without a 400 ms recovery stalling the UI. */
#define TOUCH_FAILURES_BEFORE_BACKOFF 5
#define TOUCH_RETRY_INTERVAL_US       (1000 * 1000)

/* How long the flush callback waits for the panel to report the frame buffer
 * free before giving up on that frame.
 *
 * The wait is what keeps LVGL in step with the panel, but it happens with the
 * LVGL mutex held, so blocking forever here takes the whole UI down with it --
 * and every task calling lvgl_port_lock() with it. A frame at this panel's
 * pixel clock takes roughly 40 ms, so anything approaching this bound means the
 * panel's interrupt has stopped arriving rather than that the frame is slow. */
#define FLUSH_VSYNC_TIMEOUT_MS 500

static SemaphoreHandle_t lvgl_mux;                       // LVGL mutex for synchronization
static TaskHandle_t lvgl_task_handle = NULL;             // Handle for the LVGL task

#if LVGL_PORT_AVOID_TEAR_ENABLE && LVGL_PORT_FULL_REFRESH && (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3)
/* Triple-buffered full refresh rotates three buffers between LVGL and the
 * panel, so the flush callback and the VSYNC notifier both need to see them. */
static void *lvgl_port_rgb_last_buf = NULL;
static void *lvgl_port_rgb_next_buf = NULL;
static void *lvgl_port_flush_next_buf = NULL;
#endif

/* Waits for the panel to signal that the frame buffer can be reused.
 *
 * Bounded, and complains once per outage rather than once per frame: a panel
 * whose interrupt has died would otherwise emit one line per refresh. Drawing
 * continues either way -- without the sync it can tear, which is a better
 * failure than a frozen panel. */
static void flush_wait_for_panel(void)
{
    static bool timeout_reported;

    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(FLUSH_VSYNC_TIMEOUT_MS)) == 0) {
        if (!timeout_reported) {
            timeout_reported = true;
            ESP_LOGW(TAG, "No frame-buffer-complete event within %d ms; drawing unsynchronised",
                     FLUSH_VSYNC_TIMEOUT_MS);
        }
    } else if (timeout_reported) {
        timeout_reported = false;
        ESP_LOGI(TAG, "Frame-buffer-complete events resumed");
    }
}

#if LVGL_PORT_AVOID_TEAR_ENABLE

#if LVGL_VERSION_MAJOR >= 9
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    lv_color_t *color_map = (lv_color_t *)px_map;
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

    /* Switch the current RGB frame buffer (zero-copy swap) */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

    if (lv_display_flush_is_last(disp)) {
        flush_wait_for_panel();
    }

    lv_display_flush_ready(disp);
}

#else /* LVGL_VERSION_MAJOR < 9 */

#if LVGL_PORT_DIRECT_MODE

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data; // Get the panel handle from driver user data
    const int offsetx1 = area->x1; // Start X coordinate of the area to flush
    const int offsetx2 = area->x2; // End X coordinate of the area to flush
    const int offsety1 = area->y1; // Start Y coordinate of the area to flush
    const int offsety2 = area->y2; // End Y coordinate of the area to flush

    /* Action after last area refresh */
    if (lv_disp_flush_is_last(drv)) {
        /* Switch the current RGB frame buffer */
        esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

        flush_wait_for_panel();
    }

    lv_disp_flush_ready(drv); // Mark the display flush as complete
}

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_RGB_BUFFER_NUMS == 2

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data; // Get the panel handle from driver user data
    const int offsetx1 = area->x1; // Start X coordinate of the area to flush
    const int offsetx2 = area->x2; // End X coordinate of the area to flush
    const int offsety1 = area->y1; // Start Y coordinate of the area to flush
    const int offsety2 = area->y2; // End Y coordinate of the area to flush

    /* Switch the current RGB frame buffer */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

    flush_wait_for_panel();

    lv_disp_flush_ready(drv); // Mark the display flush as complete
}

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3

void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data; // Get the panel handle from driver user data
    const int offsetx1 = area->x1; // Start X coordinate of the area to flush
    const int offsetx2 = area->x2; // End X coordinate of the area to flush
    const int offsety1 = area->y1; // Start Y coordinate of the area to flush
    const int offsety2 = area->y2; // End Y coordinate of the area to flush

    drv->draw_buf->buf1 = color_map; // Set buffer 1 to color_map
    drv->draw_buf->buf2 = lvgl_port_flush_next_buf; // Set buffer 2 to the next flush buffer
    lvgl_port_flush_next_buf = color_map; // Update the flush next buffer to color_map

    /* Switch the current RGB frame buffer to `color_map` */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

    lvgl_port_rgb_next_buf = color_map; // Update the next RGB buffer

    lv_disp_flush_ready(drv); // Mark the display flush as complete
}
#endif /* flush mode */

#endif /* LVGL_VERSION_MAJOR */

#else /* !LVGL_PORT_AVOID_TEAR_ENABLE */

#if LVGL_VERSION_MAJOR >= 9
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    lv_color_t *color_map = (lv_color_t *)px_map;
#else
static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
#endif
    const int offsetx1 = area->x1; // Start X coordinate of the area to flush
    const int offsetx2 = area->x2; // End X coordinate of the area to flush
    const int offsety1 = area->y1; // Start Y coordinate of the area to flush
    const int offsety2 = area->y2; // End Y coordinate of the area to flush

    /* Just copy data from the color map to the RGB frame buffer */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

#if LVGL_VERSION_MAJOR >= 9
    bool is_last = lv_display_flush_is_last(disp);
#else
    bool is_last = lv_disp_flush_is_last(drv);
#endif

    if (is_last) {
        flush_wait_for_panel();
    }

#if LVGL_VERSION_MAJOR >= 9
    lv_display_flush_ready(disp);
#else
    lv_disp_flush_ready(drv);
#endif
}

#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

#if LVGL_VERSION_MAJOR >= 9
static lv_display_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    void *buf1 = NULL;
    void *buf2 = NULL;
    int buffer_size = 0;

    if (panel_handle == NULL) {
        ESP_LOGE(TAG, "No panel handle");
        return NULL;
    }

#if LVGL_PORT_AVOID_TEAR_ENABLE
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_V_RES;
#if (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && LVGL_PORT_FULL_REFRESH
    /* With three buffers and full-refresh, one buffer is always available for rendering */
    esp_err_t fb_err = esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 3, &lvgl_port_rgb_last_buf, &buf1, &buf2);
    lvgl_port_rgb_next_buf = lvgl_port_rgb_last_buf;
    lvgl_port_flush_next_buf = buf2;
#else
    esp_err_t fb_err = esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2);
#endif
    if (fb_err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot get RGB frame buffers: %s", esp_err_to_name(fb_err));
        return NULL;
    }
#else
    /* Double buffering for smooth rendering. These are sizeable -- 800 x
     * LVGL_PORT_BUFFER_HEIGHT pixels each, and internal SRAM when
     * CONFIG_LVGL_PORT_BUF_INTERNAL is set -- so a failure here is a real
     * possibility and is reported rather than asserted: assertions are
     * compiled out in release builds (CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE),
     * which would leave LVGL drawing into a NULL buffer. */
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT;
    buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), LVGL_PORT_BUFFER_MALLOC_CAPS);
    buf2 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), LVGL_PORT_BUFFER_MALLOC_CAPS);
    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "Cannot allocate 2 x %d KB LVGL draw buffers",
                 (int)(buffer_size * sizeof(lv_color_t) / 1024));
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        return NULL;
    }
    ESP_LOGI(TAG, "LVGL buffer size: %dKB x 2", (int)(buffer_size * sizeof(lv_color_t) / 1024));
#endif

    lv_display_t *disp = lv_display_create(LVGL_PORT_H_RES, LVGL_PORT_V_RES);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Cannot create LVGL display");
#if !LVGL_PORT_AVOID_TEAR_ENABLE
        heap_caps_free(buf1);
        heap_caps_free(buf2);
#endif
        return NULL;
    }

    lv_display_render_mode_t render_mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
#if LVGL_PORT_DIRECT_MODE
    render_mode = LV_DISPLAY_RENDER_MODE_DIRECT;
#elif LVGL_PORT_FULL_REFRESH
    render_mode = LV_DISPLAY_RENDER_MODE_FULL;
#endif

    lv_display_set_buffers(disp, buf1, buf2, buffer_size * sizeof(lv_color_t), render_mode);
    lv_display_set_flush_cb(disp, flush_callback);
    lv_display_set_user_data(disp, panel_handle);
    return disp;
}
#else
static lv_disp_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    static lv_disp_draw_buf_t disp_buf = { 0 };     // Contains internal graphic buffer(s) called draw buffer(s)
    static lv_disp_drv_t disp_drv = { 0 };          // Contains LCD panel handle and callback functions

    // Allocate draw buffers used by LVGL
    void *buf1 = NULL; // Pointer for the first buffer
    void *buf2 = NULL; // Pointer for the second buffer
    int buffer_size = 0; // Size of the buffer

    if (panel_handle == NULL) {
        ESP_LOGE(TAG, "No panel handle");
        return NULL;
    }

    ESP_LOGD(TAG, "Malloc memory for LVGL buffer");
#if LVGL_PORT_AVOID_TEAR_ENABLE
    // To avoid tearing effect, at least two frame buffers are needed: one for LVGL rendering and another for RGB output
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_V_RES;
#if (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && LVGL_PORT_FULL_REFRESH
    // With three buffers and full-refresh, one buffer is always available for rendering
    esp_err_t fb_err = esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 3, &lvgl_port_rgb_last_buf, &buf1, &buf2);
    lvgl_port_rgb_next_buf = lvgl_port_rgb_last_buf; // Set the next RGB buffer
    lvgl_port_flush_next_buf = buf2; // Set the flush next buffer
#else
    esp_err_t fb_err = esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2); // Get two frame buffers
#endif
    if (fb_err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot get RGB frame buffers: %s", esp_err_to_name(fb_err));
        return NULL;
    }
#else
    // Enable double buffering in PSRAM for smooth rendering. Checked rather
    // than asserted: assertions are compiled out in release builds.
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT; // Calculate buffer size
    buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), LVGL_PORT_BUFFER_MALLOC_CAPS); // Allocate memory
    buf2 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), LVGL_PORT_BUFFER_MALLOC_CAPS); // Allocate memory
    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "Cannot allocate 2 x %d KB LVGL draw buffers",
                 (int)(buffer_size * sizeof(lv_color_t) / 1024));
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        return NULL;
    }
    ESP_LOGI(TAG, "LVGL buffer size: %dKB x 2", (int)(buffer_size * sizeof(lv_color_t) / 1024)); // Log buffer size
#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

    // Initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buffer_size); // Initialize the draw buffer

    ESP_LOGD(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv); // Initialize the display driver
    disp_drv.hor_res = LVGL_PORT_H_RES; // Set horizontal resolution
    disp_drv.ver_res = LVGL_PORT_V_RES; // Set vertical resolution
    disp_drv.flush_cb = flush_callback; // Set the flush callback
    disp_drv.draw_buf = &disp_buf; // Set the draw buffer
    disp_drv.user_data = panel_handle; // Set user data to panel handle
#if LVGL_PORT_FULL_REFRESH
    disp_drv.full_refresh = 1; // Enable full refresh
#elif LVGL_PORT_DIRECT_MODE
    disp_drv.direct_mode = 1; // Enable direct mode
#endif
    return lv_disp_drv_register(&disp_drv); // Register the display driver
}
#endif

#if LVGL_VERSION_MAJOR >= 9
static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
#else
static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)indev_drv->user_data;
#endif
    esp_lcd_touch_point_data_t point;
    uint8_t touchpad_cnt = 0;

    /* Read data from touch controller into memory.
     *
     * A transfer can fail if the I2C bus wedges, or if the GT911 stops
     * acknowledging altogether. The transfer is bounded (transaction_timeout_ms,
     * set in board.c) so neither hangs the LVGL task any more, but LVGL polls
     * the controller at the indev rate -- roughly 60 Hz, since PIN_NUM_TOUCH_INT
     * is -1 and there is no interrupt to gate on -- and a failure that persists
     * at that rate buries the log in driver-level errors we do not control.
     *
     * So back off instead: after a few consecutive failures, stop polling and
     * retry once a second, attempting a hard recovery each time. */
    static uint32_t touch_read_failures;
    static int64_t next_retry_us;

    data->state = LV_INDEV_STATE_RELEASED;

    if (tp == NULL) {
        return;
    }

    if (touch_read_failures >= TOUCH_FAILURES_BEFORE_BACKOFF) {
        if (esp_timer_get_time() < next_retry_us) {
            return;
        }
        next_retry_us = esp_timer_get_time() + TOUCH_RETRY_INTERVAL_US;
        waveshare_esp32_s3_touch_recover();
    }

    /* Both halves count as a read: a controller that acknowledges the transfer
     * but cannot be decoded is just as broken as one that does not answer, and
     * only counting the transfer would leave that case polling at 60 Hz
     * forever. */
    esp_err_t read_err = esp_lcd_touch_read_data(tp);
    if (read_err == ESP_OK) {
        read_err = esp_lcd_touch_get_data(tp, &point, &touchpad_cnt, 1);
    }

    if (read_err != ESP_OK) {
        touch_read_failures++;
        if (touch_read_failures == TOUCH_FAILURES_BEFORE_BACKOFF) {
            ESP_LOGW(TAG, "Touch read failing (%s), backing off to one retry per %d ms",
                     esp_err_to_name(read_err), (int)(TOUCH_RETRY_INTERVAL_US / 1000));
        }
        return;
    }

    if (touch_read_failures >= TOUCH_FAILURES_BEFORE_BACKOFF) {
        ESP_LOGI(TAG, "Touch recovered after %u failed reads", (unsigned)touch_read_failures);
    }
    touch_read_failures = 0;

    if (touchpad_cnt > 0) {
        data->point.x = point.x; // Set the X coordinate
        data->point.y = point.y; // Set the Y coordinate
        data->state = LV_INDEV_STATE_PRESSED; // Set state to pressed
        ESP_LOGD(TAG, "Touch position: %d,%d", point.x, point.y); // Log touch position
    }
}

#if LVGL_VERSION_MAJOR >= 9
static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp)
{
    lv_indev_t *indev = lv_indev_create();
    if (indev == NULL) {
        return NULL;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    lv_indev_set_user_data(indev, tp);
    return indev;
}
#else
static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp)
{
    static lv_indev_drv_t indev_drv_tp; // Static input device driver

    /* Register a touchpad input device */
    lv_indev_drv_init(&indev_drv_tp); // Initialize the input device driver
    indev_drv_tp.type = LV_INDEV_TYPE_POINTER; // Set the device type to pointer (touchpad)
    indev_drv_tp.read_cb = touchpad_read; // Set the read callback function
    indev_drv_tp.user_data = tp; // Set user data to the touch panel handle

    return lv_indev_drv_register(&indev_drv_tp); // Register the input device driver
}
#endif

static void tick_increment(void *arg)
{
    /* Tell LVGL how many milliseconds have elapsed */
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS); // Increment the LVGL tick count
}

static esp_err_t tick_init(void)
{
    // Tick interface for LVGL, driven by a periodic esp_timer
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &tick_increment, // Set the callback function for the timer
        .name = "LVGL tick" // Name of the timer
    };
    esp_timer_handle_t lvgl_tick_timer = NULL; // Timer handle
    esp_err_t ret = esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    if (ret != ESP_OK) {
        return ret;
    }
    return esp_timer_start_periodic(lvgl_tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000); // Start the timer
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGD(TAG, "Starting LVGL task"); // Log the task start

    uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS; // Set initial task delay
    while (1) {
        if (lvgl_port_lock(-1)) { // Try to lock the LVGL mutex
            task_delay_ms = lv_timer_handler(); // Handle LVGL timer events
            lvgl_port_unlock(); // Unlock the mutex
        }
        // Ensure the delay time is within limits
        if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms)); // Delay the task for the calculated time
    }
}

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle)
{
    if (lcd_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_init(); // Initialize LVGL

    esp_err_t ret = tick_init(); // Initialize the tick timer
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start the LVGL tick timer: %s", esp_err_to_name(ret));
        return ret;
    }

#if LVGL_VERSION_MAJOR >= 9
    lv_display_t *disp = display_init(lcd_handle); // Initialize the display (v9)
#else
    lv_disp_t *disp = display_init(lcd_handle); // Initialize the display (v8)
#endif
    if (disp == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_lcd_rgb_panel_event_callbacks_t cbs = {
    #if RGB_BOUNCE_BUFFER_SIZE > 0
        /* In bounce-buffer mode the panel fires this once per frame, as the
         * bounce position wraps; on_vsync is not the event to wait on here. */
        .on_frame_buf_complete = rgb_lcd_on_vsync_event,
    #else
        .on_vsync = rgb_lcd_on_vsync_event,
    #endif
    };
    ret = esp_lcd_rgb_panel_register_event_callbacks(lcd_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cannot register panel event callbacks: %s", esp_err_to_name(ret));
        return ret;
    }

    /* A panel with no touch controller is a usable panel, so a failure here
     * costs the input device and nothing else. */
    if (tp_handle) {
        if (indev_init(tp_handle) == NULL) {
            ESP_LOGE(TAG, "Cannot create the touch input device; continuing without touch");
        }
    }

    lvgl_mux = xSemaphoreCreateRecursiveMutex(); // Create a recursive mutex for LVGL
    if (lvgl_mux == NULL) {
        ESP_LOGE(TAG, "Cannot create the LVGL mutex");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Create LVGL task"); // Log task creation
    BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE; // Determine core ID for the task
    BaseType_t task_ret = xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, NULL,
                                                  LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id); // Create the LVGL task
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task"); // Log error if task creation fails
        vSemaphoreDelete(lvgl_mux);
        lvgl_mux = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK; // Return success
}

bool lvgl_port_lock(int timeout_ms)
{
    if (lvgl_mux == NULL) {
        ESP_LOGE(TAG, "lvgl_port_init must be called first");
        return false;
    }

    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms); // Convert timeout to ticks
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE; // Try to take the mutex
}

void lvgl_port_unlock(void)
{
    if (lvgl_mux == NULL) {
        return;
    }
    xSemaphoreGiveRecursive(lvgl_mux); // Release the mutex
}

bool lvgl_port_notify_rgb_vsync(void)
{
    BaseType_t need_yield = pdFALSE; // Flag to check if a yield is needed
#if LVGL_PORT_FULL_REFRESH && (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3)
    if (lvgl_port_rgb_next_buf != lvgl_port_rgb_last_buf) {
        lvgl_port_flush_next_buf = lvgl_port_rgb_last_buf; // Set next buffer for flushing
        lvgl_port_rgb_last_buf = lvgl_port_rgb_next_buf; // Update the last buffer
    }
#else
    /* Always notify the LVGL task on VSYNC to synchronize rendering.
     *
     * eIncrement, not eNoAction: the waiter is ulTaskNotifyTake(), which counts.
     * eNoAction happens to unblock it too, but leaves the count at zero, so a
     * notification arriving just before the wait starts would be lost. */
    if (lvgl_task_handle) {
        xTaskNotifyFromISR(lvgl_task_handle, 0, eIncrement, &need_yield);
    }
#endif
    return (need_yield == pdTRUE); // Return whether a yield is needed
}
