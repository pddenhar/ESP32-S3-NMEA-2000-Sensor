# NMEA2000_twai (vendored)

Transport layer connecting the NMEA2000 library to the ESP32 CAN controller.
Subclasses `tNMEA2000` and talks to the hardware through the ESP-IDF TWAI
driver, so it works on the ESP32-S3.

**Upstream:** https://github.com/skarlsson/NMEA2000_twai (MIT, see `LICENSE`)

## Why vendored rather than a git dependency

Its `CMakeLists.txt` declares `REQUIRES NMEA2000 driver`, which no longer builds
on ESP-IDF 6: the monolithic `driver` component was split and `driver/gpio.h`
moved to `esp_driver_gpio`. Fixing that in `managed_components/` would not
survive a clean checkout, since that directory is gitignored and regenerated.

## Local changes

Only `CMakeLists.txt` is modified. `NMEA2000_esp32.cpp` and `NMEA2000_esp32.h`
are byte-for-byte upstream, so re-syncing later is a straight file copy.

- `REQUIRES` gained `esp_driver_gpio` and `esp_driver_twai`.
- `SRCS` lists the one source file instead of `FILE(GLOB_RECURSE ./*.*)`, which
  had been pulling `CMakeLists.txt` and `idf_component.yml` in as sources.

## Note on `CONFIG_TWAI_ISR_IN_IRAM`

The upstream source raises `#warning` when this is unset, which is an error
under IDF's `-Werror`. It is enabled in `sdkconfig.defaults`, which is also the
correct setting: it keeps the TWAI ISR out of flash so CAN timing does not
depend on cache hits.

## Do not confuse with ttlappalainen/NMEA2000_esp32

Despite the same class name (`tNMEA2000_esp32`), that library hardcodes the
ESP32 CAN peripheral base address `0x3ff6b000` and cannot work on the S3, where
TWAI lives at `0x6002b000`.
