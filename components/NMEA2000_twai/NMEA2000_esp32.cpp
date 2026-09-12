#include "NMEA2000_esp32.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"

#define TAG "NMEA2000_esp32"

tNMEA2000_esp32::tNMEA2000_esp32(
    gpio_num_t TxPin,
    gpio_num_t RxPin,
    int twai_controller_id,
    CAN_speed_t can_speed) : tNMEA2000(),
                             node_(nullptr),
                             rx_queue_(nullptr),
                             rx_queue_depth_(40),
                             tx_next_slot_(0),
                             is_open_(false),
                             error_monitor_task_handle_(nullptr),
                             should_stop_error_monitor_(false),
                             error_monitor_running_(false),
                             bus_off_(false),
                             recover_requested_(false),
                             can_mutex_(xSemaphoreCreateMutex()),
                             not_open_reported_(false),
                             busoff_reported_(false)
{
    (void)twai_controller_id; // esp_twai picks a free controller itself

    memset(&node_config_, 0, sizeof(node_config_));
    memset(rx_scratch_, 0, sizeof(rx_scratch_));
    memset(tx_slots_, 0, sizeof(tx_slots_));
    tx_lock_ = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

    node_config_.io_cfg.tx = TxPin;
    node_config_.io_cfg.rx = RxPin;
    node_config_.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    node_config_.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    /* The enum values are already in kbps. */
    node_config_.bit_timing.bitrate = static_cast<uint32_t>(can_speed) * 1000;
    node_config_.tx_queue_depth = kTxSlotCount;
    /* Retry forever rather than dropping frames: on a busy NMEA 2000 bus,
     * losing arbitration is routine, not an error. */
    node_config_.fail_retry_cnt = -1;
}

tNMEA2000_esp32::~tNMEA2000_esp32()
{
    should_stop_error_monitor_ = true;
    // Wait for the monitor task to exit on its own (it deletes itself when it
    // sees the stop flag) rather than force-deleting it, so it is never killed
    // while holding can_mutex_. Capped so a wedged task cannot hang teardown.
    for (int i = 0; error_monitor_running_ && i < 500; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    CAN_deinit();
    if (rx_queue_ != nullptr)
    {
        vQueueDelete(rx_queue_);
        rx_queue_ = nullptr;
    }
    if (can_mutex_ != nullptr)
    {
        vSemaphoreDelete(can_mutex_);
    }
}

void tNMEA2000_esp32::SetCANBufferSize(uint16_t RxBufferSize, uint16_t TxBufferSize)
{
    rx_queue_depth_ = RxBufferSize;
    /* The TX depth cannot exceed the slot pool: every queued frame needs a slot
     * to keep its data alive while the driver holds a pointer to it. */
    node_config_.tx_queue_depth = (TxBufferSize > kTxSlotCount) ? kTxSlotCount : TxBufferSize;
}

bool tNMEA2000_esp32::GetBusHealth(BusHealth &out)
{
    out.open = false;
    out.bus_off = bus_off_;
    out.error_state = bus_off_ ? TWAI_ERROR_BUS_OFF : TWAI_ERROR_ACTIVE;
    out.tx_error_count = 0;
    out.rx_error_count = 0;

    twai_node_status_t status;
    xSemaphoreTake(can_mutex_, portMAX_DELAY);
    esp_err_t err = (is_open_ && node_ != nullptr)
                        ? twai_node_get_info(node_, &status, nullptr)
                        : ESP_ERR_INVALID_STATE;
    xSemaphoreGive(can_mutex_);

    if (err != ESP_OK) {
        return false;
    }

    out.open = true;
    out.error_state = status.state;
    out.tx_error_count = status.tx_error_count;
    out.rx_error_count = status.rx_error_count;
    return true;
}

void tNMEA2000_esp32::InitCANFrameBuffers()
{
    // Set default buffer sizes if not set by user
    if (rx_queue_depth_ == 0) rx_queue_depth_ = 50;
    if (node_config_.tx_queue_depth == 0) node_config_.tx_queue_depth = kTxSlotCount;

    tNMEA2000::InitCANFrameBuffers();
}

/* ----------------------------------------------------------------------------
 * ISR callbacks. The driver rejects registration of callbacks that are not in
 * IRAM, so all three carry IRAM_ATTR. Nothing here may log or block.
 * -------------------------------------------------------------------------- */

bool IRAM_ATTR tNMEA2000_esp32::onRxDone(twai_node_handle_t handle,
                                         const twai_rx_done_event_data_t *edata,
                                         void *user_ctx)
{
    (void)edata;
    auto *self = static_cast<tNMEA2000_esp32 *>(user_ctx);
    BaseType_t higher_woken = pdFALSE;

    twai_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    /* The driver validates that this buffer is in DRAM; rx_scratch_ is a member
     * so it never lives on a task stack that might sit in PSRAM. */
    frame.buffer = self->rx_scratch_;
    frame.buffer_len = sizeof(self->rx_scratch_);

    if (twai_node_receive_from_isr(handle, &frame) == ESP_OK)
    {
        RxItem item;
        item.id = frame.header.id;
        /* Classic CAN only: DLC is the byte count. Clamped rather than run
         * through twaifd_dlc2len(), whose HAL_ASSERT has no place in an ISR. */
        uint16_t len = frame.header.dlc;
        if (len > sizeof(item.data)) len = sizeof(item.data);
        item.len = static_cast<uint8_t>(len);
        memcpy(item.data, self->rx_scratch_, item.len);

        if (self->rx_queue_ != nullptr)
        {
            /* Drops the frame if the queue is full, which is the same outcome
             * the legacy driver fixed-size RX queue produced. */
            xQueueSendFromISR(self->rx_queue_, &item, &higher_woken);
        }
    }

    return higher_woken == pdTRUE;
}

bool IRAM_ATTR tNMEA2000_esp32::onTxDone(twai_node_handle_t handle,
                                         const twai_tx_done_event_data_t *edata,
                                         void *user_ctx)
{
    (void)handle;
    auto *self = static_cast<tNMEA2000_esp32 *>(user_ctx);

    /* Release the exact slot the driver just finished with. is_tx_success is
     * ignored deliberately: either way the driver is done with the frame, and
     * holding the slot back would leak it. */
    if (edata != nullptr && edata->done_tx_frame != nullptr)
    {
        portENTER_CRITICAL_ISR(&self->tx_lock_);
        for (size_t i = 0; i < kTxSlotCount; i++)
        {
            if (&self->tx_slots_[i].frame == edata->done_tx_frame)
            {
                self->tx_slots_[i].in_use = false;
                break;
            }
        }
        portEXIT_CRITICAL_ISR(&self->tx_lock_);
    }

    return false;
}

bool IRAM_ATTR tNMEA2000_esp32::onStateChange(twai_node_handle_t handle,
                                              const twai_state_change_event_data_t *edata,
                                              void *user_ctx)
{
    (void)handle;
    auto *self = static_cast<tNMEA2000_esp32 *>(user_ctx);
    if (edata != nullptr)
    {
        self->bus_off_ = (edata->new_sta == TWAI_ERROR_BUS_OFF);
    }
    return false;
}

/* ------------------------------------------------------------------------- */

bool tNMEA2000_esp32::CANOpen()
{
    if (is_open_) return true;

    if (rx_queue_ == nullptr)
    {
        rx_queue_ = xQueueCreate(rx_queue_depth_, sizeof(RxItem));
        if (rx_queue_ == nullptr)
        {
            ESP_LOGE(TAG, "Failed to allocate RX queue");
            return false;
        }
    }

    CAN_init();
    if (!is_open_)
    {
        /* CAN_init() logs the specific failure. Reporting it here too is what
         * keeps tNMEA2000::Open() -- and everything that trusts its return --
         * from announcing a bus this node never joined. */
        return false;
    }

    if (error_monitor_task_handle_ == nullptr &&
        xTaskCreate(errorMonitorTask, "TWAI_errMonitor", TWAI_ERROR_MONITOR_STACK_SIZE, this, 5,
                    &error_monitor_task_handle_) == pdPASS)
    {
        error_monitor_running_ = true;
    }
    return true;
}

void tNMEA2000_esp32::CAN_init()
{
    xSemaphoreTake(can_mutex_, portMAX_DELAY);
    if (!busoff_reported_)
    {
        ESP_LOGI(TAG, "Initializing TWAI driver");
    }

    esp_err_t result = twai_new_node_onchip(&node_config_, &node_);
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create TWAI node: %s", esp_err_to_name(result));
        node_ = nullptr;
        xSemaphoreGive(can_mutex_);
        return;
    }

    /* Callbacks and filters must be configured while the node is stopped --
     * the driver rejects both once it has been enabled. */
    twai_event_callbacks_t cbs = {};
    cbs.on_rx_done = onRxDone;
    cbs.on_tx_done = onTxDone;
    cbs.on_state_change = onStateChange;
    result = twai_node_register_event_callbacks(node_, &cbs, this);
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register TWAI callbacks: %s", esp_err_to_name(result));
        twai_node_delete(node_);
        node_ = nullptr;
        xSemaphoreGive(can_mutex_);
        return;
    }

    /* Accept everything: mask bits are "1 = must match", so an all-zero mask
     * matches any ID. NMEA 2000 filtering happens in the protocol layer. */
    twai_mask_filter_config_t filter = {};
    filter.id = 0;
    filter.mask = 0;
    filter.is_ext = 1;
    result = twai_node_config_mask_filter(node_, 0, &filter);
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure TWAI filter: %s", esp_err_to_name(result));
        twai_node_delete(node_);
        node_ = nullptr;
        xSemaphoreGive(can_mutex_);
        return;
    }

    result = twai_node_enable(node_);
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable TWAI node: %s", esp_err_to_name(result));
        twai_node_delete(node_);
        node_ = nullptr;
        xSemaphoreGive(can_mutex_);
        return;
    }

    if (!busoff_reported_)
    {
        ESP_LOGI(TAG, "TWAI driver started successfully");
    }
    is_open_ = true;
    xSemaphoreGive(can_mutex_);
}

void tNMEA2000_esp32::CAN_deinit()
{
    xSemaphoreTake(can_mutex_, portMAX_DELAY);
    if (is_open_ && node_ != nullptr)
    {
        // Mark closed before tearing down so a concurrent send/receive that
        // is waiting on the mutex sees the closed state once it acquires it.
        is_open_ = false;
        if (!busoff_reported_)
        {
            ESP_LOGI(TAG, "Stopping TWAI driver");
        }
        twai_node_disable(node_);
        twai_node_delete(node_);
        node_ = nullptr;

        /* The node is gone, so nothing will ever complete these. Safe to
         * release here -- unlike during bus-off recovery, where the driver
         * re-runs the queued transmissions itself. */
        portENTER_CRITICAL(&tx_lock_);
        for (size_t i = 0; i < kTxSlotCount; i++)
        {
            tx_slots_[i].in_use = false;
        }
        portEXIT_CRITICAL(&tx_lock_);
    }
    xSemaphoreGive(can_mutex_);
}

bool tNMEA2000_esp32::CANSendFrame(unsigned long id, unsigned char len, const unsigned char* buf, bool wait_sent)
{
    (void)wait_sent;

    xSemaphoreTake(can_mutex_, portMAX_DELAY);
    if (!is_open_ || node_ == nullptr) {
        xSemaphoreGive(can_mutex_);
        if (!not_open_reported_) {
            ESP_LOGE(TAG, "CAN port not open; suppressing further messages until recovery");
            not_open_reported_ = true;
        }
        return false;
    }

    /* Claim a slot. The frame and its payload must stay alive until the driver
     * reports the transmission done, because it queues a pointer to them. */
    TxSlot *slot = nullptr;
    portENTER_CRITICAL(&tx_lock_);
    for (size_t i = 0; i < kTxSlotCount; i++)
    {
        size_t idx = (tx_next_slot_ + i) % kTxSlotCount;
        if (!tx_slots_[idx].in_use)
        {
            slot = &tx_slots_[idx];
            slot->in_use = true;
            tx_next_slot_ = (idx + 1) % kTxSlotCount;
            break;
        }
    }
    portEXIT_CRITICAL(&tx_lock_);

    if (slot == nullptr)
    {
        /* All slots in flight: same outcome as the legacy driver full TX
         * queue. The NMEA2000 library buffers above this layer and retries. */
        xSemaphoreGive(can_mutex_);
        return false;
    }

    const uint8_t dlc = static_cast<uint8_t>(len > 8 ? 8 : len);
    memcpy(slot->data, buf, dlc);

    memset(&slot->frame, 0, sizeof(slot->frame));
    slot->frame.header.id = id;
    slot->frame.header.ide = 1; // NMEA 2000 is 29-bit extended IDs throughout
    slot->frame.header.dlc = dlc;
    slot->frame.buffer = slot->data;
    slot->frame.buffer_len = dlc;

    // Never block on transmit. The driver has its own TX queue for buffering,
    // and the NMEA2000 library maintains a send buffer above this layer.
    // Blocking here (e.g. 100ms per frame) causes catastrophic event loop
    // stalls when the CAN bus is faulty or unterminated and the controller
    // enters bus-off recovery.
    esp_err_t result = twai_node_transmit(node_, &slot->frame, 0);
    if (result != ESP_OK)
    {
        portENTER_CRITICAL(&tx_lock_);
        slot->in_use = false;
        portEXIT_CRITICAL(&tx_lock_);
    }
    xSemaphoreGive(can_mutex_);
    return (result == ESP_OK);
}

bool tNMEA2000_esp32::CANGetFrame(unsigned long& id, unsigned char& len, unsigned char* buf)
{
    if (!is_open_ || rx_queue_ == nullptr) {
        if (!not_open_reported_) {
            ESP_LOGE(TAG, "CAN port not open; suppressing further messages until recovery");
            not_open_reported_ = true;
        }
        return false;
    }

    /* Non-blocking drain of the queue the RX ISR callback fills. No mutex
     * needed: the FreeRTOS queue is the synchronisation point, and taking
     * can_mutex_ here would contend with the transmit path on every poll. */
    RxItem item;
    if (xQueueReceive(rx_queue_, &item, 0) != pdTRUE)
    {
        return false;
    }

    // A received frame means the bus is alive again: re-arm the report-once
    // flags so a future outage is reported.
    if (busoff_reported_ || not_open_reported_) {
        ESP_LOGI(TAG, "CAN bus recovered");
        busoff_reported_ = false;
        not_open_reported_ = false;
    }
    id = item.id;
    len = item.len;
    memcpy(buf, item.data, item.len);
    return true;
}

void tNMEA2000_esp32::errorMonitorTask(void* pvParameters)
{
    auto* instance = static_cast<tNMEA2000_esp32*>(pvParameters);

    // Timestamp for the last high-error-counter warning (in milliseconds)
    uint32_t last_highcounter_log = 0;
    const uint32_t LOG_INTERVAL = 60000; // 1 minute in milliseconds

    while (!instance->should_stop_error_monitor_)
    {
        uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());

        if (instance->bus_off_)
        {
            instance->handleBusError();
        }
        else
        {
            if (instance->recover_requested_)
            {
                ESP_LOGI(TAG, "TWAI recovered from bus-off");
                instance->recover_requested_ = false;
            }

            twai_node_status_t status;
            xSemaphoreTake(instance->can_mutex_, portMAX_DELAY);
            esp_err_t err = (instance->node_ != nullptr)
                                ? twai_node_get_info(instance->node_, &status, nullptr)
                                : ESP_ERR_INVALID_STATE;
            xSemaphoreGive(instance->can_mutex_);

            if (err == ESP_OK &&
                (status.tx_error_count > 127 || status.rx_error_count > 127))
            {
                if (current_time - last_highcounter_log >= LOG_INTERVAL)
                {
                    ESP_LOGW(TAG, "High error counters detected: TX=%u, RX=%u",
                             (unsigned)status.tx_error_count, (unsigned)status.rx_error_count);
                    last_highcounter_log = current_time;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
    }

    instance->error_monitor_running_ = false;
    vTaskDelete(nullptr);
}

void tNMEA2000_esp32::handleBusError()
{
    // Report once per outage; the flag (cleared on recovery in CANGetFrame)
    // also silences the per-cycle reinit logs in CAN_deinit/CAN_init.
    if (!busoff_reported_) {
        ESP_LOGE(TAG, "Bus-off; requesting recovery, suppressing further messages until recovered");
        busoff_reported_ = true;
    }

    /* twai_node_recover() replaces the legacy uninstall/reinstall cycle.
     * Recovery completes only after the controller sees 128 consecutive
     * occurrences of 11 recessive bits, reported through onStateChange, so this
     * is requested once rather than every second.
     *
     * Queued frames are deliberately left alone: the driver restarts pending
     * transmissions itself when the node returns to error-active, and freeing
     * their slots here would let them be overwritten while still queued. */
    if (!recover_requested_)
    {
        xSemaphoreTake(can_mutex_, portMAX_DELAY);
        if (node_ != nullptr)
        {
            esp_err_t err = twai_node_recover(node_);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            {
                ESP_LOGE(TAG, "twai_node_recover failed: %s", esp_err_to_name(err));
            }
        }
        xSemaphoreGive(can_mutex_);
        recover_requested_ = true;
    }
}
