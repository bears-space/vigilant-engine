#include "narrowband.h"
#include <RadioLib.h>
#include "EspHal.h"
#include "esp_log.h"
#include <cstring>
#include <cstdint>
#include <sys/types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

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
        const UBaseType_t rxtxTaskNotifyIndex = 1; // index of the notification value used for receive ISR flag
        
        void handleReceive();
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
        radio.setPacketTransmittedAction(transmit_isr);

        // create the rx/tx task
        // xTaskCreate(rxtx_task_trampoline, "narrowband_rxtx", 4096, this, 1, NULL);

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

    void NarrowbandRadio::handleReceive() {

        size_t len = radio.getPacketLength();
        uint8_t* buf = (uint8_t*)malloc(len + 1); // +1 for null terminator

        int state = radio.readData(buf, len);

        if (state == RADIOLIB_ERR_NONE) {
            // segfault if data is not null-terminated
            buf[len] = '\0';
            ESP_LOGI(TAG, "Received packet: %s\n", buf);
            // maybe parse command here, currently we just enqueue the raw packet data for processing in the main thread
            // enqueueCommand(buf);
        } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
            ESP_LOGI(TAG, "Received packet with CRC mismatch!\n");
        } else {
            ESP_LOGI(TAG, "Failed to read received packet, code %d\n", state);
        }

        free(buf);
    }

    void NarrowbandRadio::rxtx_task_trampoline(void* param) {
        static_cast<NarrowbandRadio*>(param)->rxtx_task();
    }

    void NarrowbandRadio::rxtx_task() {
        ESP_LOGI(TAG, "[LLCC68] Started rxtx task!\n");
        rxtxTaskHandle = xTaskGetCurrentTaskHandle();
        uint32_t ulNotificationValue;

        int state = RADIOLIB_ERR_NONE;
        while (true) {
            ESP_LOGI(TAG, "[LLCC68] Sending packet...");

            // TODO: replace with sensor data from sensor queue
            state = radio.startTransmit("Hello world!");

            if (state != RADIOLIB_ERR_NONE) {
                ESP_LOGI(TAG, "\nstartTransmit failed, code %d\n", state);
            }

            ulNotificationValue = ulTaskNotifyTakeIndexed(rxtxTaskNotifyIndex, pdTRUE, pdMS_TO_TICKS(tx_timeout_ms));
            if (ulNotificationValue == 1) {
                state = radio.finishTransmit();
                if (state != RADIOLIB_ERR_NONE) {
                    ESP_LOGI(TAG, "\nTransmission failed during finishTransmit, code %d\n", state);
                } else {
                    ESP_LOGI(TAG, " done!\n");
                }
            } else {
                ESP_LOGI(TAG, "\nTransmission timeout, no callback received within %d ms\n", tx_timeout_ms);
            }


            state = radio.startReceive();
            if (state == RADIOLIB_ERR_NONE) {
                ESP_LOGI(TAG, "Waiting for a packet...\n");
            } else {
                ESP_LOGI(TAG, "failed to start receiver, code %d\n", state);
            }

            // receive for 0.5 s (the amount specified by rxtx_interval_ms)
            TickType_t start = xTaskGetTickCount();
            uint16_t elapsed_time_ms = 0;
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
    }

}

// C COMPATIBILITY WRAPPERS

extern "C" {
    void init_narrowband(QueueHandle_t* commandQueue, QueueHandle_t* sensorDataQueue) {
        nb_radio.init(commandQueue, sensorDataQueue);
    }
}
