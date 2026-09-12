
#include "esp_log.h"
#include "nvs_flash.h"
#include "bsp/lvgl_port.h"
#include "bsp/board.h"
#include "ui/ui_main.h"
#include "sensors/rudder_sensor.h"
#include "n2k/n2k_bridge.h"

static const char *TAG = "main";

/* Holds the chosen gauges across power cycles. A failure here is not fatal:
 * the panel falls back to its default selection and simply forgets changes. */
static void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* A partition written by another build, or one that filled up. Neither
         * holds anything this device cannot recreate. */
        ESP_LOGW(TAG, "Reformatting NVS: %s", esp_err_to_name(ret));
        if (nvs_flash_erase() == ESP_OK) {
            ret = nvs_flash_init();
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    }
}

void app_main(void)
{
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    nvs_init();

    /* Initialize LCD and touch hardware. A touch controller that fails to come
     * up is reported by the driver and leaves touch_handle NULL; only a panel
     * or bus failure lands here, and without a panel there is nothing to show. */
    esp_err_t ret = waveshare_esp32_s3_rgb_lcd_init(&panel_handle, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Hardware driver initialization failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    // Initialize LVGL porting layer
    ret = lvgl_port_init(panel_handle, touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port layer initialization failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    // Turn on screen backlight
    ret = waveshare_rgb_lcd_bl_on();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Backlight enabling failed: %s", esp_err_to_name(ret));
        // Proceed anyway as the display/touch driver is still running
    }


    /* Start the rudder feedback capture before the UI so the first poll has a
     * chance of finding a reading. A failure here is not fatal: the gauge shows
     * its no-data state and the rest of the panel stays usable. */
    rudder_sensor_config_t rudder_cfg = RUDDER_SENSOR_DEFAULT_CONFIG();
    ret = rudder_sensor_init(&rudder_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Rudder sensor init failed: %s", esp_err_to_name(ret));
    }

    /* Start NMEA 2000 output once the sensor exists. Also non-fatal: without
     * the bus the gauge still works, it just publishes nothing. */
    ret = n2k_bridge_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NMEA 2000 bridge start failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Building application UI");

    if (lvgl_port_lock(-1)) {
        ui_main_create();
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock, UI not created");
    }

    ESP_LOGI(TAG, "UI tasks running via background worker task. Deleting main task.");

    vTaskDelete(NULL);
}