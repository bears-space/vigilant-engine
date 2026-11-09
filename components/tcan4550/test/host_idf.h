/*
# SPDX-AI-Disclosure: ai-generated
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: Test mocks for ESP-IDF and FreeRTOS APIs.
# SPDX-AI-Date: 2026-10-13
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_NOT_SUPPORTED 4
#define ESP_ERR_INVALID_SIZE 5
#define ESP_ERR_NOT_FOUND 6
#define ESP_ERR_NO_MEM 7
#define ESP_ERR_INVALID_RESPONSE 8
#define ESP_RETURN_ON_ERROR(expr, tag, ...) \
    do {                                    \
        esp_err_t e_ = (expr);              \
        if (e_ != ESP_OK) return e_;        \
    } while (0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)

typedef int gpio_num_t;
#define GPIO_NUM_NC -1
typedef int spi_host_device_t;
typedef void* spi_device_handle_t;
typedef struct {
    size_t length;
    const void* tx_buffer;
    void* rx_buffer;
} spi_transaction_t;
typedef struct {
    int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num,
        max_transfer_sz;
} spi_bus_config_t;
typedef struct {
    int clock_speed_hz, mode, spics_io_num, queue_size;
} spi_device_interface_config_t;
#define SPI_DMA_CH_AUTO 0
esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t*);
esp_err_t spi_bus_initialize(spi_host_device_t, const spi_bus_config_t*, int);
esp_err_t spi_bus_add_device(spi_host_device_t,
                             const spi_device_interface_config_t*,
                             spi_device_handle_t*);
esp_err_t spi_bus_remove_device(spi_device_handle_t);
esp_err_t spi_bus_free(spi_host_device_t);

#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
void* heap_caps_calloc(size_t, size_t, unsigned);
void heap_caps_free(void*);

typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
typedef struct mock_mutex* SemaphoreHandle_t;
#define configTICK_RATE_HZ 100U
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / 10U))
#define pdTRUE 1
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void vSemaphoreDelete(SemaphoreHandle_t);
int xSemaphoreTake(SemaphoreHandle_t, TickType_t);
int xSemaphoreGive(SemaphoreHandle_t);
TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t);
