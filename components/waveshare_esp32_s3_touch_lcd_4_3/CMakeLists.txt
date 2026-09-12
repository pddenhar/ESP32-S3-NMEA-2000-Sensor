idf_component_register(SRCS "src/board.c"
                            "src/lvgl_port.c"
                    INCLUDE_DIRS "include"
                    REQUIRES  "esp_timer"
                              "esp_lcd" "esp_lcd_touch" "esp_lcd_touch_gt911"
                              "lvgl"
                              # board.h is public and includes driver/gpio.h and
                              # driver/i2c_master.h; esp_hw_support carries the
                              # GPIO reservation API the touch reset revokes.
                              "esp_driver_gpio" "esp_driver_i2c" "esp_hw_support"
)