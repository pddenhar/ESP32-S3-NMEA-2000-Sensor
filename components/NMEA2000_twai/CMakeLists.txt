# Sources are listed explicitly. Upstream used FILE(GLOB_RECURSE sources ./*.*),
# which also swept CMakeLists.txt and idf_component.yml into SRCS.
#
# REQUIRES differs from upstream's `NMEA2000 driver`: ESP-IDF 6 split the
# monolithic `driver` component, so it no longer provides driver/gpio.h. That
# now comes from esp_driver_gpio. `driver` is kept because the legacy
# driver/twai.h wrapper this code uses still lives there, and esp_driver_twai is
# named explicitly so the dependency survives `driver` shrinking further.
idf_component_register(
    SRCS "NMEA2000_esp32.cpp"
    INCLUDE_DIRS "."
    REQUIRES NMEA2000 driver esp_driver_gpio esp_driver_twai
)

# ESP-IDF 6.1 marks the legacy driver/twai.h as deprecated with a #warning,
# which -Werror turns fatal.
#
# PUBLIC, not PRIVATE: NMEA2000_esp32.h has twai_*_config_t members, so it must
# include driver/twai.h, and every consumer of this component inherits the
# warning too. Attaching the downgrade here keeps it with the component that
# causes it, rather than scattering -Wno-error=cpp across each consumer.
# Everything else in the project keeps warnings-as-errors.
#
# The legacy driver still works in 6.1 but will be removed eventually; the real
# fix is porting this shim to the esp_twai.h API. See README.md.
target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-error=cpp)
