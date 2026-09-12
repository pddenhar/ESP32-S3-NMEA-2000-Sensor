#ifndef _RGB_LCD_H_
#define _RGB_LCD_H_

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_interface.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"


#define CONFIG_LCD_TOUCH_CONTROLLER_GT911 1 // 1 initiates the touch, 0 closes the touch.

#define I2C_MASTER_SCL_IO           9       /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           8       /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0       /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ          400000                     /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

/* GT911 interrupt line (CTP_IRQ on the schematic).
 *
 * Driven low only while the controller is held in reset, because the GT911
 * latches its I2C address from this pin's level as reset is released: low
 * selects 0x5D, high selects 0x14. It is released back to an input afterwards
 * -- it is the controller's output the rest of the time, and holding it would
 * put two drivers on the same net. */
#define GPIO_TOUCH_INT      4
#define GPIO_TOUCH_INT_SEL  (1ULL << GPIO_TOUCH_INT)
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define LCD_H_RES               800
#define LCD_V_RES               480
#define LCD_PIXEL_CLOCK_HZ      (14 * 1000 * 1000)
#define LCD_BIT_PER_PIXEL       (16)
#define RGB_BIT_PER_PIXEL       (16)
#define RGB_DATA_WIDTH          (16)
#define RGB_BOUNCE_BUFFER_SIZE  (LCD_H_RES * CONFIG_LCD_RGB_BOUNCE_BUFFER_HEIGHT)
#define LCD_IO_RGB_DISP         (-1)             // -1 if not used
#define LCD_IO_RGB_VSYNC        (GPIO_NUM_3)
#define LCD_IO_RGB_HSYNC        (GPIO_NUM_46)
#define LCD_IO_RGB_DE           (GPIO_NUM_5)
#define LCD_IO_RGB_PCLK         (GPIO_NUM_7)
#define LCD_IO_RGB_DATA0        (GPIO_NUM_14)
#define LCD_IO_RGB_DATA1        (GPIO_NUM_38)
#define LCD_IO_RGB_DATA2        (GPIO_NUM_18)
#define LCD_IO_RGB_DATA3        (GPIO_NUM_17)
#define LCD_IO_RGB_DATA4        (GPIO_NUM_10)
#define LCD_IO_RGB_DATA5        (GPIO_NUM_39)
#define LCD_IO_RGB_DATA6        (GPIO_NUM_0)
#define LCD_IO_RGB_DATA7        (GPIO_NUM_45)
#define LCD_IO_RGB_DATA8        (GPIO_NUM_48)
#define LCD_IO_RGB_DATA9        (GPIO_NUM_47)
#define LCD_IO_RGB_DATA10       (GPIO_NUM_21)
#define LCD_IO_RGB_DATA11       (GPIO_NUM_1)
#define LCD_IO_RGB_DATA12       (GPIO_NUM_2)
#define LCD_IO_RGB_DATA13       (GPIO_NUM_42)
#define LCD_IO_RGB_DATA14       (GPIO_NUM_41)
#define LCD_IO_RGB_DATA15       (GPIO_NUM_40)

#define LCD_IO_RST              (-1)             // -1 if not used
#define PIN_NUM_BK_LIGHT        (-1)    // -1 if not used
#define LCD_BK_LIGHT_ON_LEVEL   (1)
#define LCD_BK_LIGHT_OFF_LEVEL  !LCD_BK_LIGHT_ON_LEVEL

#define PIN_NUM_TOUCH_RST       (-1)            // -1 if not used
#define PIN_NUM_TOUCH_INT       (-1)            // -1 if not used


/**
 * @brief Bring up the RGB panel and, if present, the GT911 touch controller.
 *
 * A panel with no working touch controller is still a usable panel, so a touch
 * failure is reported in the log and leaves @p ret_touch NULL rather than
 * failing the call; only a panel or I2C bus failure returns an error.
 *
 * @param[out] ret_panel  panel handle; never NULL on success
 * @param[out] ret_touch  touch handle, or NULL if touch did not come up
 */
esp_err_t waveshare_esp32_s3_rgb_lcd_init(esp_lcd_panel_handle_t *ret_panel, esp_lcd_touch_handle_t *ret_touch);

/**
 * @brief Handle for the shared board I2C bus (GT911 touch + CH422G expander).
 *
 * NULL until waveshare_esp32_s3_rgb_lcd_init() has run. Exposed so callers can
 * recover the bus with i2c_master_bus_reset() after a transfer times out.
 */
i2c_master_bus_handle_t waveshare_esp32_s3_i2c_bus_handle(void);

/**
 * @brief Recover the touch controller after repeated I2C failures.
 *
 * Resets the I2C master, logs which devices still acknowledge on the bus, then
 * hard-resets the GT911 through the CH422G expander. Takes ~400 ms (the reset
 * pulse has mandatory settling delays), so call it from a backoff path, not on
 * every failed read.
 */
esp_err_t waveshare_esp32_s3_touch_recover(void);

esp_err_t waveshare_rgb_lcd_bl_on(void);
esp_err_t waveshare_rgb_lcd_bl_off(void);

#endif