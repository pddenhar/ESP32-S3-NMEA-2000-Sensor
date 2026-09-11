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

---

## Decoding a CAN Capture

`tools/n2k_decode.py` turns a raw CAN capture into NMEA 2000 terms: it splits the
29-bit identifiers into PGN, priority and source address, and expands the PGNs
this project produces into named fields. Standard library only, no install step.

### Capturing

In SavvyCAN, **File > Save Captured Data**, saved as *Generic CSV* or *GVRET*.
The expected layout is `Time Stamp,ID,Extended,Bus,LEN,D1..D8`; the `Dir` column
that some SavvyCAN configurations add is handled too, since columns are located
by header name rather than fixed position. Timestamps are microseconds.

### Running

```sh
python tools/n2k_decode.py can_log.csv       # whole capture
python tools/n2k_decode.py can_log.csv --pgn 127245   # rudder frames only
python tools/n2k_decode.py can_log.csv --src 25       # one device only
```

`--pgn` and `--src` are repeatable, and combine as an AND. With no file
argument the capture is read from stdin, so it can sit in a pipe.

Each line is one frame:

```
  4312776925  src=25     bcast  pri=2  PGN 127245 Rudder   00 F8 FF 7F 48 D0 FF FF  instance=0 position=-70.0 deg order=n/a direction=no order
```

Standard-ID frames are skipped -- NMEA 2000 is extended-ID only, so anything
with an 11-bit identifier is something else sharing the wire.

PGNs 127245 (Rudder), 60928 (ISO Address Claim) and 59904 (ISO Request) are
expanded field by field; a handful more are named but print as raw bytes, and
anything unrecognised shows as a bare PGN number with its payload. Fields carrying
the NMEA 2000 "not available" value print as `n/a` rather than as `3276.7 deg`.

The per-PGN summary at the end goes to **stderr**, and the frames to stdout, so
redirecting the frames to a file still shows the totals on the terminal:

```sh
python tools/n2k_decode.py can_log.csv > frames.txt
```