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

## NMEA 2000 Data

Everything on the panel is a *channel*: a value, its units, how long it stays
good, and where it came from. Rudder angle is not a special case for being
measured here rather than received -- it is simply the channel whose data
originates on this device. The table lives in `main/n2k/n2k_channels.c`.

**Out:** PGN 127245 (Rudder), 10 Hz, from the RF300 above.

**In:** PGN 127489 (Engine Parameters, Dynamic) feeds two channels, coolant
temperature and oil pressure. One message can feed any number of channels --
each reading the panel can draw is its own -- so a field that goes missing
blanks its own gauge while the others keep updating. Only engine instance 0 (a
single or port engine) is decoded: a twin-screw boat sends this PGN once per
engine, often from the same ECU, so the source address does not separate them.
See `N2K_ENGINE_INSTANCE` in `n2k_bridge.cpp`.

Engine readouts are in °F and psi. Set `N2K_ENGINE_UNITS_US` to 0 in
`n2k_channels.h` for °C and kPa; the units label, the dial range and the alarm
points all follow from that one switch.

**In:** PGN 127250 (Vessel Heading), shown on the heading gauge. The PGN carries
a flag saying whether the heading is referenced to true or to magnetic north --
the two differ by the local variation, so the gauge always labels which one it
is showing rather than leaving it to be assumed. A heading is blanked after 3 s
of silence from its sender. If two devices send the same PGN, the first one
heard from keeps the channel until it goes quiet -- otherwise the readout
alternates between senders that disagree by a degree or two.

### Choosing gauges

The hamburger at the top left opens a drawer listing every channel that has a
gauge to draw it, with its PGN and a dot that lights green while data is
arriving -- so a gauge can be selected before its sender is powered up, and a
silent sender can be told apart from a missing one. Two gauges fit on screen;
with both slots full the remaining rows are disabled rather than letting a new
choice silently evict a gauge that is being watched. The selection is saved to
NVS a couple of seconds after the last change and restored on boot.

### Adding a PGN

Four small edits, none of them to layout or timer code:

1. An id in `n2k_channel_id_t` and a row in `s_channels[]`
   (`main/n2k/n2k_channels.c`) giving the key, PGN, display name, units and
   timeout.
2. A `case` in the receive handler in `main/n2k/n2k_bridge.cpp` that parses the
   message and calls `n2k_channels_publish()`. Add the PGN to
   `kReceiveMessages[]` too, so the device declares what it listens for.
3. A gauge. Anything that is a number between two bounds needs no new widget:
   write a `ui_dial_spec_t` and one `UI_DIAL_GAUGE_CLASS_DEFINE` line, as
   `main/ui/ui_engine_gauges.c` does. A reading that needs its own shape gets a
   widget exporting a `ui_gauge_class_t` -- see the adapter at the bottom of
   `main/ui/ui_heading_gauge.c`, two functions, create and update.
4. That class in `s_gauge_classes[]` in `main/ui/ui_main.c`.

The **key** is what the saved selection is stored under, so it has to be unique
per channel and must never be renamed once shipped. It cannot be the PGN:
several channels share 127489, and a PGN would not say which of them the user
chose.

Leave step 4 out and the channel is decoded and transmitted but stays out of the
menu, which is a reasonable place to stop until the widget exists.

Locally measured channels differ only in having a `pull` function in the table
instead of being published into: the value is fetched from its driver on demand
rather than cached, which is what keeps the rudder's 100 Hz display path intact.

### Bus status indicator

The header line reports what the device can actually tell about its connection:

| Reading | Meaning |
| --- | --- |
| `offline` | The bridge did not start, or the CAN port failed to open |
| `bus off` | Controller in bus-off; recovery requested |
| `no response` | Frames go out, nothing acknowledges them -- unplugged drop cable, or no terminator |
| `claiming address` | Bus is healthy, ISO address claim still in progress |
| `online (addr N)` | Address claimed, controller error-active |

The error state of the CAN controller is what drives this, rather than waiting
for received traffic to dry up: a node alone on the wire climbs out of
error-active within a few transmit attempts, whereas a quiet-but-healthy bus
may send nothing for a minute between heartbeats. A *fault* is held for three
seconds before a better reading is believed, because a disconnected bus cycles
through bus-off and recovery continuously and would otherwise flicker. Claiming
an address is not held: it happens once at startup, finishes in about a quarter
of a second, and never oscillates.

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