# NMEA2000_twai (vendored, ported to esp_twai.h)

Transport layer connecting the NMEA2000 library to the ESP32 CAN controller.
Subclasses `tNMEA2000` and talks to the hardware through the ESP-IDF TWAI
driver, so it works on the ESP32-S3.

**Upstream:** https://github.com/skarlsson/NMEA2000_twai (MIT, see `LICENSE`)

## Why vendored rather than a git dependency

Upstream targets the legacy `driver/twai.h`, which ESP-IDF 6.1 deprecates with a
`#warning` that `-Werror` turns fatal. Its `CMakeLists.txt` also declares
`REQUIRES NMEA2000 driver`, which no longer builds on IDF 6 since `driver/gpio.h`
moved to `esp_driver_gpio`. Fixing either in `managed_components/` would not
survive a clean checkout, as that directory is gitignored and regenerated.

## Local changes

The public class API is unchanged, so callers need no modification.

- **Ported from `driver/twai.h` to `esp_twai.h` / `esp_twai_onchip.h`.** No
  legacy TWAI symbols remain in the binary, and the project builds with
  warnings-as-errors intact.
- `REQUIRES` is now `NMEA2000 esp_driver_gpio esp_driver_twai`.
- `SRCS` lists the one source file instead of `FILE(GLOB_RECURSE ./*.*)`, which
  had been pulling `CMakeLists.txt` and `idf_component.yml` in as sources.

## Three things the new driver forced, worth knowing before editing

**1. TX frames must outlive the call.** `twai_node_transmit()` pushes a
*pointer* to the `twai_frame_t` onto its queue -- there is no copy anywhere in
the path (`twai_frame_queue.c` stores `const twai_frame_t *data`). The legacy
`twai_transmit_v2()` took its message by value, so a stack local was fine; here
it would dangle while queued. Every in-flight frame therefore holds a slot in
`tx_slots_` until `on_tx_done` reports it sent, identified precisely via
`twai_tx_done_event_data_t::done_tx_frame`.

Slots are deliberately *not* reclaimed on bus-off: the driver restarts pending
transmissions itself once the node returns to error-active
(`esp_twai_onchip.c`, "node recover from busoff, restart remain tx
transaction"). Freeing them there would let a queued frame be overwritten.

**2. The callbacks must be in IRAM.** `twai_node_register_event_callbacks()`
rejects an `on_tx_done` that is not (`esp_ptr_in_iram` check), so all three
carry `IRAM_ATTR`. They run in ISR context: no logging, no blocking calls.
Callbacks and filters must also be registered *before* `twai_node_enable()`;
the driver refuses both once the node is running.

**3. There is no blocking receive.** `twai_receive()` is gone;
`twai_node_receive_from_isr()` may only be called inside `on_rx_done`. Frames
are copied into a FreeRTOS queue there, which `CANGetFrame()` drains
non-blocking, preserving the polling contract the NMEA2000 library expects.

Bus-off recovery is now `twai_node_recover()` rather than the old
uninstall/reinstall cycle, requested once per outage and confirmed through the
`on_state_change` callback.

## Note on `CONFIG_TWAI_ISR_IN_IRAM`

Enabled in `sdkconfig.defaults`. It keeps the TWAI ISR out of flash so CAN
timing does not depend on cache hits. The `#warning` that upstream raised
without it is gone along with the legacy driver, but the setting is still the
right one.

## Do not confuse with ttlappalainen/NMEA2000_esp32

Despite the same class name (`tNMEA2000_esp32`), that library hardcodes the
ESP32 CAN peripheral base address `0x3ff6b000` and cannot work on the S3, where
TWAI lives at `0x6002b000`.
