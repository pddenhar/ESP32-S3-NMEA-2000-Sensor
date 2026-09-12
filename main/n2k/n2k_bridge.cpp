#include "n2k_bridge.h"

#include <cmath>

#include "NMEA2000_esp32.h"
#include "N2kMessages.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "n2k_channels.h"
}

static const char *TAG = "n2k";

/* CAN pins on the Waveshare ESP32-S3-Touch-LCD-4.3B. */
#define N2K_CAN_TX_GPIO GPIO_NUM_15
#define N2K_CAN_RX_GPIO GPIO_NUM_16

#define N2K_RUDDER_INSTANCE 0

/* Which engine's parameters to display. 0 is a single or port engine. */
#define N2K_ENGINE_INSTANCE 0

/* PGN 127245 is normally transmitted at 10 Hz. */
#define N2K_RUDDER_PERIOD_MS 100

/* How often the controller is polled for its error state. The state changes
 * over several frame times, so there is nothing to gain from polling faster,
 * and each poll takes the driver's mutex. */
#define N2K_STATUS_PERIOD_MS 200

/* How long a bad bus state is held before a better one is believed. Long
 * enough to cover a bus-off recovery cycle, short enough that plugging the
 * drop cable back in shows up while the hand is still on the connector. */
#define N2K_STATUS_HOLD_MS 3000

/* The stack has to be serviced far more often than the transmit period so
 * address claims and ISO requests are answered promptly. */
#define N2K_SERVICE_PERIOD_MS 5

#define N2K_TASK_STACK_SIZE 4096
#define N2K_TASK_PRIORITY 4
/* Core 0: the LVGL port task is pinned to core 1 (CONFIG_LVGL_PORT_TASK_CORE). */
#define N2K_TASK_CORE 0

/* NMEA 2000 device identity.
 *
 * TODO: verify the class/function pair against the NMEA 2000 "class & function
 * codes" document. 40/155 is the widely used pairing for a rudder sensor, but
 * the library ships only a link to the PDF, not the table itself.
 *
 * The manufacturer code is the one the library's own examples use for devices
 * without a registered code. A shipping product needs a real code issued by the
 * NMEA -- this value is fine on a test bench and not fine on a customer's boat. */
#define N2K_DEVICE_CLASS 40     /* Steering and Control Surfaces */
#define N2K_DEVICE_FUNCTION 155 /* Rudder */
#define N2K_MANUFACTURER_CODE 2046
#define N2K_PRODUCT_CODE 100
#define N2K_PREFERRED_ADDRESS 25

/* Declaring what we transmit and receive lets other devices discover this
 * node's interface without having to observe traffic. */
static const unsigned long kTransmitMessages[] = {127245UL, 0};
static const unsigned long kReceiveMessages[] = {127250UL, 127488UL, 127489UL, 0};

/* The library keeps the address-claim state protected, since it is only
 * meaningful to the code servicing the stack. This node does service the stack,
 * so it exposes the one query it needs rather than guessing from the source
 * address (which has a valid-looking value throughout the claim). */
class tN2kBridgeDevice : public tNMEA2000_esp32 {
public:
    using tNMEA2000_esp32::tNMEA2000_esp32;

    /* Mutates claim state on timeout, so it must only be called from the task
     * that calls ParseMessages(). */
    bool AddressClaimInProgress() { return IsAddressClaimStarted(0); }
};

static tN2kBridgeDevice s_nmea2000(N2K_CAN_TX_GPIO, N2K_CAN_RX_GPIO);
static volatile bool s_running = false;
static char s_serial[16];

/* Shared with the UI task. Written only by the N2K task; the spinlock keeps a
 * reader from seeing half of an update. Decoded values go to n2k_channels
 * instead; what is left here is the bus's own state. */
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static n2k_bridge_status_t s_status;

/* A NAME that collides with another node's breaks address claiming, so the
 * unique number is derived from the factory MAC rather than hardcoded. */
static unsigned long unique_number_from_mac(void)
{
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        return 1;
    }
    /* The NAME field is 21 bits wide. */
    unsigned long id = ((unsigned long)mac[3] << 16) | ((unsigned long)mac[4] << 8) | mac[5];
    return id & 0x1FFFFFUL;
}

/* Called from ParseMessages() for every message that arrives, in the N2K task. */
static void handle_heading(const tN2kMsg &msg)
{
    unsigned char sid = 0;
    double heading_rad = N2kDoubleNA;
    double deviation_rad = N2kDoubleNA;
    double variation_rad = N2kDoubleNA;
    tN2kHeadingReference ref = N2khr_Unavailable;

    if (!ParseN2kHeading(msg, sid, heading_rad, deviation_rad, variation_rad, ref)) {
        return;
    }

    /* A compass that is powered but not yet settled sends the PGN with the
     * heading marked not available, and some send it with the reference field
     * unset. Neither is something to display, but both are normal traffic --
     * the reading simply goes stale and the gauge blanks. */
    if (N2kIsNA(heading_rad) || (ref != N2khr_true && ref != N2khr_magnetic)) {
        return;
    }

    double heading_deg = RadToDeg(heading_rad);
    /* Wrapped rather than clamped: a sender is free to use a full-circle value
     * just outside 0..2pi, and a heading is modular anyway. */
    heading_deg = std::fmod(heading_deg, 360.0);
    if (heading_deg < 0.0) {
        heading_deg += 360.0;
    }

    /* Publishing, rather than storing, is what applies the shared rules: which
     * sender owns the channel when several send the same PGN, and how long the
     * value stays good. */
    n2k_reading_t reading = {};
    reading.valid = true;
    reading.value = (float)heading_deg;
    reading.qualifier = (ref == N2khr_magnetic) ? N2K_HEADING_REF_MAGNETIC : N2K_HEADING_REF_TRUE;
    reading.source_address = msg.Source;
    reading.timestamp_us = esp_timer_get_time();
    n2k_channels_publish(N2K_CH_HEADING, &reading);
}

/* PGN 127488 also carries boost pressure and tilt/trim; only what the panel
 * draws is decoded, and the rest can be added a channel at a time. */
static void handle_engine_rapid(const tN2kMsg &msg)
{
    unsigned char instance = 0;
    double speed_rpm = N2kDoubleNA;
    double boost_pressure_pa = N2kDoubleNA;
    int8_t tilt_trim = 0;

    if (!ParseN2kEngineParamRapid(msg, instance, speed_rpm, boost_pressure_pa, tilt_trim)) {
        return;
    }
    if (instance != N2K_ENGINE_INSTANCE || N2kIsNA(speed_rpm)) {
        return;
    }

    n2k_reading_t reading = {};
    reading.valid = true;
    reading.value = (float)speed_rpm;
    reading.source_address = msg.Source;
    reading.timestamp_us = esp_timer_get_time();
    n2k_channels_publish(N2K_CH_ENGINE_SPEED, &reading);
}

/* One message, several channels: PGN 127489 carries a dozen engine readings and
 * each one the panel can draw is published separately, so a gauge blanks on its
 * own if its field is absent while the others keep updating. */
static void handle_engine_dynamic(const tN2kMsg &msg)
{
    unsigned char instance = 0;
    double oil_pressure_pa = N2kDoubleNA;
    double oil_temp_k = N2kDoubleNA;
    double coolant_temp_k = N2kDoubleNA;
    double alternator_v = N2kDoubleNA;
    double fuel_rate = N2kDoubleNA;
    double engine_hours = N2kDoubleNA;
    double coolant_pressure_pa = N2kDoubleNA;
    double fuel_pressure_pa = N2kDoubleNA;
    int8_t load_pct = 0;
    int8_t torque_pct = 0;

    if (!ParseN2kEngineDynamicParam(msg, instance, oil_pressure_pa, oil_temp_k,
                                    coolant_temp_k, alternator_v, fuel_rate, engine_hours,
                                    coolant_pressure_pa, fuel_pressure_pa, load_pct,
                                    torque_pct)) {
        return;
    }

    /* A twin-screw boat sends this PGN once per engine, and both can come from
     * the same ECU -- so the source address does not separate them and the
     * channel would show whichever arrived last. Only the configured engine is
     * decoded until there is a gauge that says which engine it is showing. */
    if (instance != N2K_ENGINE_INSTANCE) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();

    n2k_reading_t reading = {};
    reading.valid = true;
    reading.source_address = msg.Source;
    reading.timestamp_us = now_us;

    if (!N2kIsNA(coolant_temp_k)) {
        reading.value = n2k_temp_from_kelvin(coolant_temp_k);
        n2k_channels_publish(N2K_CH_ENGINE_TEMP, &reading);
    }

    if (!N2kIsNA(oil_pressure_pa)) {
        reading.value = n2k_pressure_from_pascal(oil_pressure_pa);
        n2k_channels_publish(N2K_CH_ENGINE_OIL_PRESSURE, &reading);
    }
}

static void n2k_msg_handler(const tN2kMsg &msg)
{
    /* Every decoded message counts as proof of life on the bus, including the
     * address claims and heartbeats of devices whose data we ignore. */
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_state_lock);
    s_status.rx_msg_count++;
    s_status.last_rx_us = now_us;
    portEXIT_CRITICAL(&s_state_lock);

    switch (msg.PGN) {
    case 127250UL:
        handle_heading(msg);
        break;
    case 127488UL:
        handle_engine_rapid(msg);
        break;
    case 127489UL:
        handle_engine_dynamic(msg);
        break;
    default:
        break;
    }
}

/* Runs in the N2K task: AddressClaimInProgress() touches stack state. */
static void status_update(void)
{
    tN2kBridgeDevice::BusHealth health;
    const bool have_health = s_nmea2000.GetBusHealth(health);
    /* Asked before the state is decided so the claim timer keeps being
     * serviced even while the bus is faulty. */
    const bool claiming = s_nmea2000.AddressClaimInProgress();

    n2k_bus_state_t state;
    if (!s_running || !have_health) {
        state = N2K_BUS_STOPPED;
    } else if (health.bus_off || health.error_state == TWAI_ERROR_BUS_OFF) {
        state = N2K_BUS_OFF;
    } else if (health.error_state == TWAI_ERROR_PASSIVE) {
        /* Error-passive with a transmitting node almost always means nothing is
         * acknowledging our frames: no other device, or a wiring/termination
         * fault. Error-warning is left alone, since a busy bus touches it
         * transiently. */
        state = N2K_BUS_ERROR;
    } else if (claiming) {
        state = N2K_BUS_CLAIMING;
    } else {
        state = N2K_BUS_ONLINE;
    }

    /* A disconnected bus does not settle on one state: the controller reaches
     * bus-off, recovers, comes back error-active with the counters cleared, and
     * climbs out again within a second or two. Reporting each step verbatim
     * makes the indicator flicker between three readings while the fault is
     * unchanged, so a fault is held briefly and a recovery has to outlast it.
     * The enum is ordered worst-first, which is what makes the comparison work.
     *
     * Only faults are held. Claiming an address is a one-off startup step that
     * finishes in about a quarter of a second and never oscillates, so holding
     * it would just mean the panel reported "claiming" for three seconds after
     * the device was already online. */
    static n2k_bus_state_t held_state = N2K_BUS_ONLINE;
    static int64_t hold_until_us = 0;

    const int64_t now_us = esp_timer_get_time();
    if (state <= held_state || now_us >= hold_until_us) {
        held_state = state;
        hold_until_us = (state <= N2K_BUS_ERROR)
                            ? now_us + (int64_t)N2K_STATUS_HOLD_MS * 1000
                            : 0;
    } else {
        state = held_state;
    }

    const uint8_t address = s_nmea2000.GetN2kSource();

    n2k_bridge_status_t published;
    portENTER_CRITICAL(&s_state_lock);
    s_status.state = state;
    s_status.address = address;
    s_status.tx_error_count = health.tx_error_count;
    s_status.rx_error_count = health.rx_error_count;
    published = s_status;
    portEXIT_CRITICAL(&s_state_lock);

    static n2k_bus_state_t logged_state = N2K_BUS_STOPPED;
    static bool logged_once = false;
    if (!logged_once || published.state != logged_state) {
        logged_once = true;
        logged_state = published.state;
        ESP_LOGI(TAG, "Bus %s: address %u, TEC %u, REC %u, %lu messages received",
                 n2k_bus_state_str(published.state), (unsigned)published.address,
                 (unsigned)published.tx_error_count, (unsigned)published.rx_error_count,
                 (unsigned long)published.rx_msg_count);
    }
}

static void n2k_task(void *arg)
{
    (void)arg;

    TickType_t last_send = xTaskGetTickCount();
    TickType_t last_status = last_send;

    for (;;) {
        /* Runs address claiming and answers ISO requests. Must be called often. */
        s_nmea2000.ParseMessages();

        if ((xTaskGetTickCount() - last_status) >= pdMS_TO_TICKS(N2K_STATUS_PERIOD_MS)) {
            last_status = xTaskGetTickCount();
            status_update();
        }

        if ((xTaskGetTickCount() - last_send) >= pdMS_TO_TICKS(N2K_RUDDER_PERIOD_MS)) {
            /* Advance by exactly one period so the cadence does not drift with
             * the service loop. If the task was starved for longer than that,
             * resynchronise instead of catching up: the missed messages carry
             * angles that are already stale, and sending them back to back at
             * the service rate would put a burst on the bus in place of a
             * steady 10 Hz. */
            const TickType_t now = xTaskGetTickCount();
            last_send += pdMS_TO_TICKS(N2K_RUDDER_PERIOD_MS);
            if ((now - last_send) >= pdMS_TO_TICKS(N2K_RUDDER_PERIOD_MS)) {
                last_send = now;
            }

            /* Read through the channel rather than the sensor directly, so what
             * goes on the wire and what appears on the gauge are the same value
             * from the same conversion. */
            n2k_reading_t reading;
            /* N2kDoubleNA is NMEA 2000's "not available". Sending it keeps the
             * PGN cadence steady while telling listeners the reading is absent,
             * which is what makes a display blank the field instead of showing
             * a rudder that looks centred. */
            double position_rad = N2kDoubleNA;
            if (n2k_channels_read(N2K_CH_RUDDER, &reading) == ESP_OK && reading.valid) {
                position_rad = DegToRad(reading.value);
            }

            tN2kMsg msg;
            SetN2kRudder(msg, position_rad, N2K_RUDDER_INSTANCE);
            s_nmea2000.SendMsg(msg);
        }

        vTaskDelay(pdMS_TO_TICKS(N2K_SERVICE_PERIOD_MS));
    }
}

extern "C" esp_err_t n2k_bridge_start(void)
{
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    const unsigned long unique_number = unique_number_from_mac();
    snprintf(s_serial, sizeof(s_serial), "%lu", unique_number);

    s_nmea2000.SetProductInformation(s_serial,
                                     N2K_PRODUCT_CODE,
                                     "Rudder Bridge",
                                     "1.0.0",
                                     "1.0.0");
    s_nmea2000.SetDeviceInformation(unique_number,
                                    N2K_DEVICE_FUNCTION,
                                    N2K_DEVICE_CLASS,
                                    N2K_MANUFACTURER_CODE);

    /* N2km_NodeOnly: this device claims an address and transmits, but does not
     * forward received traffic anywhere. Forwarding is disabled explicitly
     * because the library defaults to echoing to a stream we do not set up. */
    s_nmea2000.SetMode(tNMEA2000::N2km_NodeOnly, N2K_PREFERRED_ADDRESS);
    s_nmea2000.EnableForward(false);
    s_nmea2000.ExtendTransmitMessages(kTransmitMessages);
    s_nmea2000.ExtendReceiveMessages(kReceiveMessages);

    /* Node-only mode does not forward traffic anywhere, but received messages
     * still reach this handler -- that is how the bus data shown on screen and
     * the proof-of-life for the status indicator arrive. */
    s_nmea2000.SetMsgHandler(n2k_msg_handler);

    if (!s_nmea2000.Open()) {
        ESP_LOGE(TAG, "Failed to open CAN port on TX=GPIO%d RX=GPIO%d",
                 (int)N2K_CAN_TX_GPIO, (int)N2K_CAN_RX_GPIO);
        return ESP_FAIL;
    }

    /* Set before the task exists: its first status_update() reads this, and a
     * task that started promptly would otherwise report the bus as stopped. */
    s_running = true;

    if (xTaskCreatePinnedToCore(n2k_task, "n2k", N2K_TASK_STACK_SIZE, nullptr,
                                N2K_TASK_PRIORITY, nullptr, N2K_TASK_CORE) != pdPASS) {
        /* The CAN port stays open: the library exposes no way to close it, and
         * a controller that is up but unserviced is harmless -- it acknowledges
         * frames and nothing else. Failing here means the system is out of
         * memory, so there is nothing useful to retry with either. */
        ESP_LOGE(TAG, "Failed to create NMEA 2000 task; CAN port left open and unserviced");
        s_running = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "NMEA 2000 started: TX=GPIO%d RX=GPIO%d, unique number %lu, PGN 127245 every %d ms",
             (int)N2K_CAN_TX_GPIO, (int)N2K_CAN_RX_GPIO, unique_number, N2K_RUDDER_PERIOD_MS);
    return ESP_OK;
}

extern "C" bool n2k_bridge_is_running(void)
{
    return s_running;
}

extern "C" void n2k_bridge_get_status(n2k_bridge_status_t *out)
{
    if (out == nullptr) {
        return;
    }

    portENTER_CRITICAL(&s_state_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_state_lock);
}

extern "C" const char *n2k_bus_state_str(n2k_bus_state_t state)
{
    switch (state) {
    case N2K_BUS_STOPPED:  return "stopped";
    case N2K_BUS_OFF:      return "bus off";
    case N2K_BUS_ERROR:    return "no response";
    case N2K_BUS_CLAIMING: return "claiming";
    case N2K_BUS_ONLINE:   return "online";
    }
    return "unknown";
}
