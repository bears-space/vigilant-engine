#include <RadioLib.h>
#include "EspHal.h"
#include "esp_log.h"
#include <cstring>
#include <cstdint>
#include <sys/types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "narrowband.h"
#include "message.h"

namespace {

    static const char* TAG = "Narrowband";

    // CLASS DEFINITION
    class NarrowbandRadio {
    private:

        // pin definitions
        static constexpr int SCLK_PIN = 14;
        static constexpr int MISO_PIN = 12;
        static constexpr int MOSI_PIN = 13;
        static constexpr int NSS_PIN  = 19;
        static constexpr int DIO1_PIN = 26;
        static constexpr int NRST_PIN = 18;
        static constexpr int BUSY_PIN = 21;
        static constexpr int RXEN_PIN = 16;

        static constexpr uint16_t rxtx_interval_ms = 500;
        static constexpr uint32_t tx_timeout_ms = 500;

        EspHal hal;
        Module module;
        LLCC68 radio;

        QueueHandle_t* commandQueue;
        QueueHandle_t* sensorDataQueue;
        
        TaskHandle_t rxtxTaskHandle;
        static constexpr UBaseType_t rxtxTaskNotifyIndex = 1; // index of the notification value used for receive ISR flag

        void handleReceive();
        std::array<uint8_t, 256> pack_messages();
        void transmit_sensor_data();
        void listen_for_command();
        static void IRAM_ATTR transmit_isr(void);
        static void IRAM_ATTR receive_isr(void);
        static void rxtx_task_trampoline(void* param);
        void rxtx_task();
        
    public:
        NarrowbandRadio();
        void init(QueueHandle_t* commandQueue, QueueHandle_t* sensorDataQueue);
    };

    // static instance of the narrowband class
    NarrowbandRadio nb_radio;

    // CLASS IMPLEMENTATION

    NarrowbandRadio::NarrowbandRadio()
        : hal(SCLK_PIN, MISO_PIN, MOSI_PIN),
        module(&hal, NSS_PIN, DIO1_PIN, NRST_PIN, BUSY_PIN),
        radio(&module),
        commandQueue(nullptr),
        sensorDataQueue(nullptr),
        rxtxTaskHandle(nullptr) {}

    void NarrowbandRadio::init(QueueHandle_t* commandQueue, QueueHandle_t* sensorDataQueue) {
        ESP_LOGI(TAG, "[LLCC68] Initializing narrowband radio...");
        
        // TODO: remove magic numbers, use config values instead
        // freq 434 Mhz, bitrate 2.4 kHz, frequency deviation 2.4 kHz, receiver bandwidth DSB 11.7 kHz, power 22 dBm, preamble length 32 bit, TCXO voltage 0 V, useRegulatorLDO false
        int state = radio.beginFSK(434, 2.4, 2.4, 11.7, 22, 32, 0, false);
        if (state != RADIOLIB_ERR_NONE) {
            ESP_LOGE(TAG, "beginFSK failed, code %d (fatal)\n", state);
            abort(); // fatal error, cannot continue without radio
        }

        // RXEN pin: 16
        // TXEN pin controlled via dio2
        radio.setRfSwitchPins(RXEN_PIN, RADIOLIB_NC);
        radio.setDio2AsRfSwitch(true);

        ESP_LOGI(TAG, "success!\n");

        // for more details, see LLCC68 datasheet, this is the highest power setting, with 22 dBm set in beginFSK
        state = radio.setPaConfig(0x04, 0x00, 0x07, 0x01);
        if (state != RADIOLIB_ERR_NONE) {
            ESP_LOGE(TAG, "PA config failed, code %d (fatal)\n", state);
            abort();
        }
        ESP_LOGI(TAG, "[LLCC68] PA config configured!\n");

        // configure callback for received/transmitted packet; must be a free/IRAM-safe function
        radio.setPacketReceivedAction(receive_isr);
        radio.setPacketSentAction(transmit_isr);

        // create the rx/tx task
        xTaskCreate(rxtx_task_trampoline, "rxtx", 4096, this, 1, &rxtxTaskHandle);
        configASSERT( rxtxTaskHandle != NULL );

    }

    void IRAM_ATTR NarrowbandRadio::transmit_isr(void) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        configASSERT( nb_radio.rxtxTaskHandle != NULL );

        vTaskNotifyGiveIndexedFromISR( nb_radio.rxtxTaskHandle, nb_radio.rxtxTaskNotifyIndex, &xHigherPriorityTaskWoken );
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }

    // ISR callback stored in IRAM; just sets the atomic flag
    void IRAM_ATTR NarrowbandRadio::receive_isr(void) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        configASSERT( nb_radio.rxtxTaskHandle != NULL );

        vTaskNotifyGiveIndexedFromISR( nb_radio.rxtxTaskHandle, nb_radio.rxtxTaskNotifyIndex, &xHigherPriorityTaskWoken );
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }


    // UNFINISHED, TODO: handle defragmentation
    void NarrowbandRadio::handleReceive() {

        // TODO: maybe implement a static memory pool to avoid memory fragmentation in the long run
        size_t len = radio.getPacketLength();
        uint8_t* buf = (uint8_t*)malloc(len);

        int state = radio.readData(buf, len);

        if (state == RADIOLIB_ERR_NONE) {

            message_t msg = {
                .data = buf,
                .length = len
            };

            ESP_LOGI(TAG, "Received packet of length %d bytes\n", len);

            // msg struct is queued by copy, buffer is not, so we need to free it after dequeueing
            if (xQueueSend( *commandQueue, (void *) &msg, ( TickType_t ) 0 ) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to enqueue received command, command queue is full!\n");
            }
        } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
            ESP_LOGI(TAG, "Received packet with CRC mismatch!\n");
        } else {
            ESP_LOGI(TAG, "Failed to read received packet, code %d\n", state);
        }
    }

    // UNFINISHED
    // return value optimization makes sure no copying takes place even when returning by value, so this is efficient
    std::array<uint8_t, 256> NarrowbandRadio::pack_messages(message_t& fragment, bool last_packet) {
        std::array<uint8_t, 256> buffer; // max payload size of LLCC68 is 256 bytes
        size_t offset = 0;

        if (fragment.length > 0 && fragment.length <= buffer.size()) {
            memcpy(buffer.data(), fragment.data, fragment.length);
            offset += fragment.length;
            free(fragment.data); // free the message data after packing
        } else if (fragment.length > buffer.size()) {
            ESP_LOGE(TAG, "Fragment length %d exceeds buffer size, discarding fragment\n", fragment.length);
            free(fragment.data);
        }
        

        while (offset < buffer.size()) {
            message_t msg;
            if (xQueueReceive( *sensorDataQueue, &msg, (TickType_t) 0 ) == pdTRUE) {
                if (offset + msg.length <= buffer.size()) {
                    memcpy(buffer.data() + offset, msg.data, msg.length);
                    offset += msg.length;
                } else {
                    // we can choose to either discard the message or stop packing further messages; for now we just stop packing
                    break;
                }
                free(msg.data); // free the message data after packing
            } else {
                // no more messages in the queue
                break;
            }
        }

        return buffer;
    }

    void NarrowbandRadio::transmit_sensor_data() {

        message_t msg;
        if (xQueueReceive( *sensorDataQueue, &msg, (TickType_t) 0 ) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to receive sensor data from queue\n");
            free(msg.data);
            return;
        }

        int state = radio.startTransmit(msg.data, msg.length);
        if (state != RADIOLIB_ERR_NONE) {
            ESP_LOGI(TAG, "startTransmit failed, code %d\n", state);
            free(msg.data);
            return;
        }

        // wait for transmission to complete or timeout
        uint32_t ulNotificationValue = ulTaskNotifyTakeIndexed(rxtxTaskNotifyIndex, pdTRUE, pdMS_TO_TICKS(tx_timeout_ms));
        if (ulNotificationValue == 1) {
            state = radio.finishTransmit();
            if (state != RADIOLIB_ERR_NONE) {
                ESP_LOGI(TAG, "Transmission failed during finishTransmit, code %d\n", state);
            } else {
                ESP_LOGI(TAG, "Transmission successful!\n");
            }
        } else {
            ESP_LOGI(TAG, "Transmission timeout, no callback received within %d ms\n", tx_timeout_ms);
        }

        free(msg.data);
    }

    // TODO: send back ACKknowledgement for received commands, to give sender the option to retry if ACK is not received within a certain time frame
    void NarrowbandRadio::listen_for_command() {
        int state = radio.startReceive();
        if (state == RADIOLIB_ERR_NONE) {
            ESP_LOGI(TAG, "Waiting for a packet...\n");
        } else {
            ESP_LOGI(TAG, "failed to start receiver, code %d\n", state);
        }

        // receive for 0.5 s (the amount specified by rxtx_interval_ms)
        TickType_t start = xTaskGetTickCount();
        uint16_t elapsed_time_ms = 0;
        uint32_t ulNotificationValue;
        while (elapsed_time_ms < rxtx_interval_ms) {
            ulNotificationValue = ulTaskNotifyTakeIndexed(rxtxTaskNotifyIndex, pdTRUE, pdMS_TO_TICKS(rxtx_interval_ms - elapsed_time_ms));
            if (ulNotificationValue == 1) {
                handleReceive();
                elapsed_time_ms = pdTICKS_TO_MS(xTaskGetTickCount() - start);
            } else {
                // timeout, no packet received within the interval
                break;
            }
        }
        
        // TODO: check if this can be left out
        state = radio.finishReceive();
        if (state != RADIOLIB_ERR_NONE) {
            ESP_LOGI(TAG, "failed to finish receive, code %d\n", state);
        }
    }

    void NarrowbandRadio::rxtx_task_trampoline(void* param) {
        static_cast<NarrowbandRadio*>(param)->rxtx_task();
    }

    void NarrowbandRadio::rxtx_task() {
        ESP_LOGI(TAG, "[LLCC68] Started rxtx task!\n");

        while (true) {
            transmit_sensor_data();
            listen_for_command();
        }
    }

}

// C COMPATIBILITY WRAPPERS

extern "C" {
    void init_narrowband(QueueHandle_t* commandQueue, QueueHandle_t* sensorDataQueue) {
        nb_radio.init(commandQueue, sensorDataQueue);
    }
}
