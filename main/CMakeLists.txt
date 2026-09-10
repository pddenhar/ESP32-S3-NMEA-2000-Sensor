idf_component_register(SRCS "main.c"
                            "ui/ui_main.c"
                            "ui/ui_rudder_gauge.c"
                            "sensors/rudder_sensor.c"
                    INCLUDE_DIRS "." "ui" "sensors"
                    REQUIRES esp_driver_mcpwm esp_driver_gpio esp_timer)
