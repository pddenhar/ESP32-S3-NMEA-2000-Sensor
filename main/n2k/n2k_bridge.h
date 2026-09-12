#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NMEA 2000 interface: rudder angle out, bus data in.
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
 *
 * Received PGNs of interest are decoded in the same task and cached for the UI
 * to poll. Like the rudder sensor, there is no queue and no callback: readers
 * ask for the latest value whenever they need one, and get a "no data" answer
 * when nothing recent has arrived.
 */
esp_err_t n2k_bridge_start(void);

/** True once the CAN port is open and the stack is running. */
bool n2k_bridge_is_running(void);

/* --------------------------------------------------------------------------
 * Received data
 * ------------------------------------------------------------------------ */

/** Reference frame a heading is measured against (PGN 127250). */
typedef enum {
    N2K_HEADING_REF_TRUE = 0,
    N2K_HEADING_REF_MAGNETIC,
} n2k_heading_ref_t;

typedef struct {
    /**
     * False when no usable heading is on hand: nothing received yet, the
     * sending device stopped, or it reported the heading as not available.
     * The other fields are meaningless in that case.
     */
    bool valid;
    float heading_deg;        /* 0..360, in the frame given by `reference` */
    n2k_heading_ref_t reference;
    uint8_t source_address;   /* Device that sent it, useful for diagnostics */
    int64_t timestamp_us;     /* esp_timer time the message was decoded */
} n2k_heading_t;

/**
 * Fetch the most recent vessel heading (PGN 127250). Safe to call from any task.
 *
 * Returns ESP_OK with @p out->valid set false when there is no recent reading;
 * no compass on the bus is a normal state, not an error.
 *
 * With more than one heading source on the bus, the first one heard from wins
 * and is kept until it goes quiet, so the reading does not alternate between
 * devices. `source_address` says which one it currently is.
 */
esp_err_t n2k_bridge_read_heading(n2k_heading_t *out);

/* --------------------------------------------------------------------------
 * Bus status
 * ------------------------------------------------------------------------ */

/**
 * What the device can say about its own connection to the bus.
 *
 * The states are ordered by severity, worst first, which is also the order the
 * status is decided in: a controller fault hides everything below it.
 */
typedef enum {
    N2K_BUS_STOPPED = 0,  /* Bridge not started, or the CAN port failed to open */
    N2K_BUS_OFF,          /* Controller in bus-off; recovery requested */
    N2K_BUS_ERROR,        /* Error counters high: nothing acknowledging us */
    N2K_BUS_CLAIMING,     /* Bus healthy, ISO address claim still in progress */
    N2K_BUS_ONLINE,       /* Address claimed, controller error-active */
} n2k_bus_state_t;

typedef struct {
    n2k_bus_state_t state;
    uint8_t address;         /* Our source address; meaningful from CLAIMING up */
    uint16_t tx_error_count; /* Controller error counters, for diagnostics */
    uint16_t rx_error_count;
    uint32_t rx_msg_count;   /* Messages decoded since start */
    int64_t last_rx_us;      /* esp_timer time of the last received message, 0 if none */
} n2k_bridge_status_t;

/** Snapshot the bus status. Safe to call from any task. */
void n2k_bridge_get_status(n2k_bridge_status_t *out);

/** Short human-readable form of @p state, e.g. "online". Never NULL. */
const char *n2k_bus_state_str(n2k_bus_state_t state);

#ifdef __cplusplus
}
#endif
