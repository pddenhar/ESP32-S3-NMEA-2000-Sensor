# Waveshare ESP32-S3 Touch LCD 4.3 - Rudder Sensor to NMEA 2000

This project measures input from a sensor and outputs it on the screen using LVGL. It also outputs the sensor value over the NMEA 2000 bus. 

---

## Rudder Sensor Wiring (Simrad RF300)

**Chain:** RF300 → sense resistor → RS485 screw terminal → GPIO43 → gauge.

**Sensor:** 3400 Hz = amidships. 20 Hz = 1°. So ±45° = 2500–4300 Hz.

**Why RS485:** the LCD eats every free pin. The RS485 receiver is *already* a
comparator with hysteresis (±200 mV, differential), on an accessible terminal.
No comparator chip needed.

If the sensor Rsense is connected to A, bias B to half of the voltage swing range of A using a resistor divider. This will provide it with the best noise immunity and the clearest signal. 

---

## References

* **Official Wiki:** [Waveshare ESP32-S3-Touch-LCD-4.3 Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3)



## Performance Tuning

To optimize display performance and eliminate rendering stutter or system-load glitches, the following configurations can be added to the project's `sdkconfig.defaults`:

* **Isolate UI Task to Core 1:**
  ```config
  CONFIG_LVGL_PORT_TASK_CORE=1
  ```
  Moves the main UI thread off Core 0 (where Wi-Fi/Bluetooth stacks run).
* **Enable High Frequency Tick Timer:**
  ```config
  CONFIG_FREERTOS_HZ=1000
  ```
  Improves scheduling resolution for more responsive animations.
* **Bypass Assertion Verification:**
  ```config
  CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE=y
  ```
  Disables runtime assertion checks within hot graphics routines.
* **Load Graphics Routines from IRAM:**
  ```config
  CONFIG_LV_ATTRIBUTE_FAST_MEM=y
  ```
  Keeps critical code path functions in fast internal SRAM instead of external flash.
* **Increase LCD DMA Bounce Buffer Height:**
  ```config
  CONFIG_LCD_RGB_BOUNCE_BUFFER_HEIGHT=120
  ```
  Prevents display flicker/corruptions due to PSRAM bus congestion.