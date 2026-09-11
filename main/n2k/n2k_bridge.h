#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NMEA 2000 output for the rudder angle.
 *
 * Owns a background task that runs the NMEA2000 protocol stack on the CAN
 * (TWAI) port and transmits PGN 127245 (Rudder) every 100 ms. The task also
 * services the stack continuously, which is what performs ISO address claiming
 * and answers ISO requests for product information -- the obligations a device
 * has to meet before it can share a bus with other equipment.
 *
 * Rudder values are pulled from rudder_sensor_read(). When the sensor has no
 * usable reading, the PGN is still sent, carrying the NMEA 2000 "not available"
 * value rather than a stale or zero angle, so a display shows a blank rather
 * than a rudder that appears centred.
 */
esp_err_t n2k_bridge_start(void);

/** True once the CAN port is open and the stack is running. */
bool n2k_bridge_is_running(void);

#ifdef __cplusplus
}
#endif
