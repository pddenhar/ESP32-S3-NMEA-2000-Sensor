# Sources are listed explicitly. Upstream used FILE(GLOB_RECURSE sources ./*.*),
# which also swept CMakeLists.txt and idf_component.yml into SRCS.
#
# REQUIRES differs from upstream's `NMEA2000 driver`: this port targets the
# esp_twai.h API, so it depends on esp_driver_twai directly and no longer needs
# the legacy `driver` component at all. esp_driver_gpio provides driver/gpio.h,
# which ESP-IDF 6 moved out of `driver`.
#
# No -Wno-error=cpp here: nothing includes the deprecated driver/twai.h any more,
# so the whole project builds with warnings-as-errors intact.
idf_component_register(
    SRCS "NMEA2000_esp32.cpp"
    INCLUDE_DIRS "."
    REQUIRES NMEA2000 esp_driver_gpio esp_driver_twai
)
