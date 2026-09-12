/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * LVGL related parameters, can be adjusted by users
 *
 */
#define LVGL_PORT_H_RES             (800)
#define LVGL_PORT_V_RES             (480)
#define LVGL_PORT_TICK_PERIOD_MS    (CONFIG_LVGL_PORT_TICK)

/**
 * LVGL timer handle task related parameters, can be adjusted by users
 *
 */
#define LVGL_PORT_TASK_MAX_DELAY_MS (CONFIG_LVGL_PORT_TASK_MAX_DELAY_MS)    // The maximum delay of the LVGL timer task, in milliseconds
#define LVGL_PORT_TASK_MIN_DELAY_MS (CONFIG_LVGL_PORT_TASK_MIN_DELAY_MS)    // The minimum delay of the LVGL timer task, in milliseconds
#define LVGL_PORT_TASK_STACK_SIZE   (CONFIG_LVGL_PORT_TASK_STACK_SIZE_KB * 1024) // The stack size of the LVGL timer task, in bytes
#define LVGL_PORT_TASK_PRIORITY     (CONFIG_LVGL_PORT_TASK_PRIORITY)        // The priority of the LVGL timer task
#define LVGL_PORT_TASK_CORE         (CONFIG_LVGL_PORT_TASK_CORE)            // The core of the LVGL timer task,
// `-1` means the don't specify the core
/**
 *
 * LVGL buffer related parameters, can be adjusted by users:
 *  (These parameters will be useless if the avoid tearing function is enabled)
 *
 *  - Memory type for buffer allocation:
 *      - MALLOC_CAP_SPIRAM: Allocate LVGL buffer in PSRAM
 *      - MALLOC_CAP_INTERNAL: Allocate LVGL buffer in SRAM
 *      (The SRAM is faster than PSRAM, but the PSRAM has a larger capacity)
 *
 */
 #if CONFIG_LVGL_PORT_BUF_PSRAM
    #define LVGL_PORT_BUFFER_MALLOC_CAPS    (MALLOC_CAP_SPIRAM)
    #elif CONFIG_LVGL_PORT_BUF_INTERNAL
    #define LVGL_PORT_BUFFER_MALLOC_CAPS    (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    #else
    #define LVGL_PORT_BUFFER_MALLOC_CAPS    (MALLOC_CAP_SPIRAM)
    #endif

    #ifdef CONFIG_LVGL_PORT_BUF_HEIGHT
    #define LVGL_PORT_BUFFER_HEIGHT         (CONFIG_LVGL_PORT_BUF_HEIGHT)
    #else
    #define LVGL_PORT_BUFFER_HEIGHT         (100)
    #endif

/**
 * Avoid tearing related configurations, can be adjusted by users.
 *
 */

#define LVGL_PORT_AVOID_TEAR_ENABLE     (CONFIG_LVGL_PORT_AVOID_TEAR_ENABLE) // Set to 1 to enable

#if LVGL_PORT_AVOID_TEAR_ENABLE
/**
 * Set the avoid tearing mode:
 *      - 0: Disable avoid tearing function
 *      - 1: LCD double-buffer & LVGL full-refresh
 *      - 2: LCD triple-buffer & LVGL full-refresh
 *      - 3: LCD double-buffer & LVGL direct-mode (recommended)
 *
 */
#define LVGL_PORT_AVOID_TEAR_MODE       (CONFIG_LVGL_PORT_AVOID_TEAR_MODE)

/**
 * Panel rotation is deliberately not supported here.
 *
 * Upstream had a rotation option that only took effect with tearing avoidance
 * enabled and was silently ignored otherwise -- the macro was not even defined
 * in that configuration, so every `#if` testing it evaluated to 0 with no
 * warning. The panel is mounted landscape; if that ever changes, rotate the
 * display through LVGL (lv_display_set_rotation) rather than reviving the
 * software pixel-rotation path this port used to carry.
 *
 * Below configurations are automatically set according to the above configurations, users do not need to modify them.
 *
 */
#if LVGL_PORT_AVOID_TEAR_MODE == 1
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (2)
#define LVGL_PORT_FULL_REFRESH          (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 2
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (3)
#define LVGL_PORT_FULL_REFRESH          (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 3
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (2)
#define LVGL_PORT_DIRECT_MODE           (1)
#endif /* LVGL_PORT_AVOID_TEAR_MODE */
#else
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (1)
#define LVGL_PORT_FULL_REFRESH          (0)
#define LVGL_PORT_DIRECT_MODE           (0)
#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

/**
 * @brief Initialize LVGL port
 *
 * @param[in] lcd_handle: LCD panel handle
 * @param[in] tp_handle: Touch panel handle
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 *      - Others: Fail
 */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle);

/**
 * @brief Take LVGL mutex
 *
 * Recursive: a task that already holds the mutex may take it again, and must
 * release it once per take.
 *
 * @param[in] timeout_ms: How long to wait. Negative blocks indefinitely; 0
 *                        returns immediately if the mutex is held elsewhere.
 *
 * @return
 *      - true:  Mutex was taken
 *      - false: Mutex was NOT taken
 */
bool lvgl_port_lock(int timeout_ms);

/**
 * @brief Give LVGL mutex
 *
 */
void lvgl_port_unlock(void);

/**
 * @brief Notifies the LVGL task when the transmission of the RGB frame buffer is completed.
 *
 * @return
 *      - true:  The tasks need to be re-scheduled
 *      - false: The tasks don't need to be re-scheduled
 */
bool lvgl_port_notify_rgb_vsync(void);

#ifdef __cplusplus
}
#endif
