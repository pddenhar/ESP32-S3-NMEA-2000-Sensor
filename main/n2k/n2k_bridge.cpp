#include "n2k_bridge.h"

#include <cmath>

#include "NMEA2000_esp32.h"
#include "N2kMessages.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "rudder_sensor.h"
}

static const char *TAG = "n2k";

/* CAN pins on the Waveshare ESP32-S3-Touch-LCD-4.3B. */
#define N2K_CAN_TX_GPIO GPIO_NUM_15
#define N2K_CAN_RX_GPIO GPIO_NUM_16

#define N2K_RUDDER_INSTANCE 0

/* PGN 127245 is normally transmitted at 10 Hz. */
#define N2K_RUDDER_PERIOD_MS 100

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

/* Declaring what we transmit lets other devices discover this node's output
 * without having to observe traffic. */
static const unsigned long kTransmitMessages[] = {127245UL, 0};

static tNMEA2000_esp32 s_nmea2000(N2K_CAN_TX_GPIO, N2K_CAN_RX_GPIO);
static volatile bool s_running = false;
static char s_serial[16];

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

static void n2k_task(void *arg)
{
    (void)arg;

    TickType_t last_send = xTaskGetTickCount();

    for (;;) {
        /* Runs address claiming and answers ISO requests. Must be called often. */
        s_nmea2000.ParseMessages();

        if ((xTaskGetTickCount() - last_send) >= pdMS_TO_TICKS(N2K_RUDDER_PERIOD_MS)) {
            last_send += pdMS_TO_TICKS(N2K_RUDDER_PERIOD_MS);

            rudder_sensor_reading_t reading;
            /* N2kDoubleNA is NMEA 2000's "not available". Sending it keeps the
             * PGN cadence steady while telling listeners the reading is absent,
             * which is what makes a display blank the field instead of showing
             * a rudder that looks centred. */
            double position_rad = N2kDoubleNA;
            if (rudder_sensor_read(&reading) == ESP_OK && reading.valid) {
                position_rad = DegToRad(reading.angle_deg);
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

    if (!s_nmea2000.Open()) {
        ESP_LOGE(TAG, "Failed to open CAN port on TX=GPIO%d RX=GPIO%d",
                 (int)N2K_CAN_TX_GPIO, (int)N2K_CAN_RX_GPIO);
        return ESP_FAIL;
    }

    if (xTaskCreatePinnedToCore(n2k_task, "n2k", N2K_TASK_STACK_SIZE, nullptr,
                                N2K_TASK_PRIORITY, nullptr, N2K_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NMEA 2000 task");
        return ESP_ERR_NO_MEM;
    }

    s_running = true;
    ESP_LOGI(TAG, "NMEA 2000 started: TX=GPIO%d RX=GPIO%d, unique number %lu, PGN 127245 every %d ms",
             (int)N2K_CAN_TX_GPIO, (int)N2K_CAN_RX_GPIO, unique_number, N2K_RUDDER_PERIOD_MS);
    return ESP_OK;
}

extern "C" bool n2k_bridge_is_running(void)
{
    return s_running;
}
