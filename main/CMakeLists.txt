idf_component_register(SRCS "main.c"
                            "ui/ui_main.c"
                            "ui/ui_rudder_gauge.c"
                            "ui/ui_heading_gauge.c"
                            "sensors/rudder_sensor.c"
                            "n2k/n2k_bridge.cpp"
                    INCLUDE_DIRS "." "ui" "sensors" "n2k"
                    REQUIRES esp_driver_mcpwm esp_driver_gpio esp_timer esp_hw_support
                             NMEA2000 NMEA2000_twai
                             waveshare_esp32_s3_touch_lcd_4_3)
