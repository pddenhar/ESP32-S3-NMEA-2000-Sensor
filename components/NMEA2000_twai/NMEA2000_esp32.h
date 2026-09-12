#pragma once

#ifndef TWAI_ERROR_MONITOR_STACK_SIZE
#define TWAI_ERROR_MONITOR_STACK_SIZE 2048
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "NMEA2000.h"

class tNMEA2000_esp32 : public tNMEA2000 {
public:
    enum class CAN_speed_t : uint32_t {
        CAN_SPEED_25KBPS = 25,
        CAN_SPEED_50KBPS = 50,
        CAN_SPEED_100KBPS = 100,
        CAN_SPEED_125KBPS = 125,
        CAN_SPEED_250KBPS = 250,
        CAN_SPEED_500KBPS = 500,
        CAN_SPEED_1000KBPS = 1000
    };

    /**
     * @param twai_controller_id  Accepted for source compatibility with the
     *        legacy-driver version of this class, but unused: the esp_twai
     *        driver allocates a free controller itself.
     */
    tNMEA2000_esp32(gpio_num_t _TxPin, gpio_num_t _RxPin, int twai_controller_id = 0,
                    CAN_speed_t = CAN_speed_t::CAN_SPEED_250KBPS);

    ~tNMEA2000_esp32();

    void SetCANBufferSize(uint16_t RxBufferSize, uint16_t TxBufferSize);

    /**
     * Controller-level view of the bus, for status reporting.
     *
     * The error state is the most useful part: a node alone on the wire (nothing
     * to acknowledge its frames, or a broken/unterminated bus) climbs out of
     * error-active within a few transmit attempts, long before the error
     * counters would reach bus-off. That makes it a far quicker "is anybody
     * there" indication than waiting for received traffic to dry up.
     */
    struct BusHealth {
        bool open;                    /* CAN port is initialised and enabled */
        bool bus_off;                 /* Latched by the state-change callback */
        twai_error_state_t error_state;
        uint16_t tx_error_count;
        uint16_t rx_error_count;
    };

    /**
     * Sample the controller state. Returns false (with @p out zeroed apart from
     * the flags it can fill) when the port is not open.
     */
    bool GetBusHealth(BusHealth &out);

protected:
    bool CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf, bool wait_sent) override;

    bool CANOpen() override;

    bool CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) override;

    void InitCANFrameBuffers() override;

private:
    void CAN_init();

    void CAN_deinit();

    static void errorMonitorTask(void *pvParameters);

    void handleBusError();

    /* The esp_twai driver requires its event callbacks to live in IRAM and
     * rejects registration otherwise, so these carry IRAM_ATTR in the .cpp.
     * They run in ISR context: no logging, no blocking calls. */
    static bool onRxDone(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx);
    static bool onTxDone(twai_node_handle_t handle, const twai_tx_done_event_data_t *edata, void *user_ctx);
    static bool onStateChange(twai_node_handle_t handle, const twai_state_change_event_data_t *edata, void *user_ctx);

    /* The new driver has no blocking receive: frames arrive in an ISR callback
     * only. They are copied into this queue so CANGetFrame() keeps its polling
     * contract with the NMEA2000 library. */
    struct RxItem {
        uint32_t id;
        uint8_t len;
        uint8_t data[8];
    };

    /* twai_node_transmit() queues the *pointer* to a twai_frame_t rather than
     * copying it, so a frame built on the stack would dangle while queued.
     * Each in-flight frame therefore owns a slot here until on_tx_done reports
     * it sent. Slots are not reclaimed on bus-off: the driver re-runs queued
     * transmissions once the node returns to error-active. */
    struct TxSlot {
        twai_frame_t frame;
        uint8_t data[8];
        bool in_use;
    };
    static constexpr size_t kTxSlotCount = 16;

    twai_onchip_node_config_t node_config_;
    twai_node_handle_t node_;
    QueueHandle_t rx_queue_;
    uint16_t rx_queue_depth_;
    uint8_t rx_scratch_[8];

    TxSlot tx_slots_[kTxSlotCount];
    size_t tx_next_slot_;
    portMUX_TYPE tx_lock_;

    bool is_open_;
    TaskHandle_t error_monitor_task_handle_;
    volatile bool should_stop_error_monitor_;
    // True while the error-monitor task is alive; lets the destructor wait for
    // the task to exit on its own instead of force-deleting it (which could
    // kill it while it holds can_mutex_ and deadlock CAN_deinit).
    volatile bool error_monitor_running_;
    // Set and cleared from the state-change ISR callback.
    volatile bool bus_off_;
    bool recover_requested_;

    // Serializes access to the node handle so the error-monitor task cannot
    // tear the driver down while another task is transmitting or receiving.
    SemaphoreHandle_t can_mutex_;
    // "Report once" flags, re-armed only on genuine recovery, to keep a
    // missing/faulty bus from flooding the log.
    bool not_open_reported_;
    bool busoff_reported_;
};
