/*
# SPDX-AI-Disclosure: ai-generated
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: ESP-IDF SPI transport implementation and failure diagnostics.
# SPDX-AI-Date: 2026-10-15
*/

#include "tcan4550_spi.h"

#include <inttypes.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tcan4550_ti.h"

#define MAX_WORDS 256U
#define MAX_BYTES (4U + MAX_WORDS * 4U)
#define SPI_ERROR_MASK 0x3F3F0000UL

static struct {
    spi_device_handle_t device;
    spi_host_device_t host;
    bool owns_bus;
    SemaphoreHandle_t mutex;
    esp_err_t error;
    uint8_t* tx;
    uint8_t* rx;
    uint8_t* status_tx;
    uint8_t* status_rx;
    size_t count;
    size_t cursor;
    uint8_t opcode;
    bool burst;
} s_spi;

static uint32_t decode(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void encode(uint8_t* p, uint32_t word) {
    p[0] = word >> 24;
    p[1] = word >> 16;
    p[2] = word >> 8;
    p[3] = word;
}

static bool session_owned(void) {
    return s_spi.mutex != NULL &&
           xSemaphoreGetMutexHolder(s_spi.mutex) == xTaskGetCurrentTaskHandle();
}

static void fail(esp_err_t error) {
    if (s_spi.error == ESP_OK) {
        s_spi.error = error;
    }
}

esp_err_t tcan4550_spi_error(void) {
    return session_owned() ? s_spi.error : ESP_ERR_INVALID_STATE;
}

esp_err_t tcan4550_ti_begin(uint32_t timeout_ms) {
    if (s_spi.mutex == NULL || session_owned()) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint64_t ticks =
        ((uint64_t)timeout_ms * configTICK_RATE_HZ + 999U) / 1000U;
    if (ticks >= portMAX_DELAY) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_spi.mutex, (TickType_t)ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_spi.error = ESP_OK;
    s_spi.burst = false;
    return ESP_OK;
}

esp_err_t tcan4550_ti_end(void) {
    if (!session_owned()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_spi.burst) {
        fail(ESP_ERR_INVALID_STATE);
        s_spi.burst = false;
    }
    const esp_err_t error = s_spi.error;
    xSemaphoreGive(s_spi.mutex);
    return error;
}

esp_err_t tcan4550_spi_deinit(void) {
    /* The caller has stopped all tasks using the singleton. */
    if (s_spi.device != NULL) {
        esp_err_t err = spi_bus_remove_device(s_spi.device);
        if (err != ESP_OK) {
            return err;
        }
        s_spi.device = NULL;
    }
    if (s_spi.owns_bus) {
        esp_err_t err = spi_bus_free(s_spi.host);
        if (err != ESP_OK) {
            return err;
        }
        s_spi.owns_bus = false;
    }
    if (s_spi.mutex != NULL) {
        vSemaphoreDelete(s_spi.mutex);
    }
    heap_caps_free(s_spi.tx);
    heap_caps_free(s_spi.rx);
    heap_caps_free(s_spi.status_tx);
    heap_caps_free(s_spi.status_rx);
    memset(&s_spi, 0, sizeof(s_spi));
    return ESP_OK;
}

esp_err_t tcan4550_spi_init(const tcan4550_config_t* config) {
    if (s_spi.device != NULL || s_spi.mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_spi.host = config->spi_host;
    s_spi.tx =
        heap_caps_calloc(1, MAX_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_spi.rx =
        heap_caps_calloc(1, MAX_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_spi.status_tx =
        heap_caps_calloc(1, 8, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_spi.status_rx =
        heap_caps_calloc(1, 8, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_spi.mutex = xSemaphoreCreateMutex();
    esp_err_t err = ESP_ERR_NO_MEM;
    if (!s_spi.tx || !s_spi.rx || !s_spi.status_tx || !s_spi.status_rx ||
        !s_spi.mutex) {
        goto cleanup;
    }
    const spi_bus_config_t bus = {
        .mosi_io_num = config->mosi_io,
        .miso_io_num = config->miso_io,
        .sclk_io_num = config->sclk_io,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = MAX_BYTES,
    };
    err = spi_bus_initialize(config->spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
        s_spi.owns_bus = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        goto cleanup;
    }
    const spi_device_interface_config_t device = {
        .clock_speed_hz = (int)config->spi_clock_hz,
        .mode = 0,
        .spics_io_num = config->cs_io,
        .queue_size = 1,
    };
    err = spi_bus_add_device(config->spi_host, &device, &s_spi.device);
    if (err == ESP_OK) {
        return ESP_OK;
    }
cleanup:
    (void)tcan4550_spi_deinit();
    return err;
}

static bool valid_address(uint16_t address, size_t count) {
    const uint32_t end = (uint32_t)address + count * 4U;
    if ((address & 3U) != 0) {
        return false;
    }
    return (address < 0x0030U && end <= 0x0030U) ||
           (address >= 0x0800U && end <= 0x0840U) ||
           (address >= 0x1000U && end <= 0x1100U) ||
           (address >= 0x8000U && end <= 0x8800U);
}

static void transfer(void) {
    if (s_spi.error != ESP_OK) {
        return;
    }
    spi_transaction_t transaction = {
        .length = (4U + s_spi.count * 4U) * 8U,
        .tx_buffer = s_spi.tx,
        .rx_buffer = s_spi.rx,
    };
    esp_err_t err = spi_device_polling_transmit(s_spi.device, &transaction);
    if (err != ESP_OK) {
        fail(err);
        return;
    }
    /* Check after the transfer too: SPIERR may describe the preceding command.
     * Status/interrupt access must remain available for diagnostics and
     * clearing. */
    const uint16_t address = ((uint16_t)s_spi.tx[1] << 8) | s_spi.tx[2];
    if (address != REG_SPI_STATUS && address != REG_DEV_IR) {
        memset(s_spi.status_tx, 0, 8);
        s_spi.status_tx[0] = AHB_READ_OPCODE;
        s_spi.status_tx[2] = REG_SPI_STATUS;
        s_spi.status_tx[3] = 1;
        spi_transaction_t status = {
            .length = 64,
            .tx_buffer = s_spi.status_tx,
            .rx_buffer = s_spi.status_rx,
        };
        err = spi_device_polling_transmit(s_spi.device, &status);
        if (err != ESP_OK) {
            fail(err);
        } else if ((s_spi.rx[0] & 8U) != 0 || (s_spi.status_rx[0] & 8U) != 0 ||
                   (decode(s_spi.status_rx + 4) & SPI_ERROR_MASK) != 0) {
            ESP_LOGE("tcan4550_spi",
                     "SPI response rejected: op=0x%02X addr=0x%04X "
                     "header=0x%08" PRIX32 " data=0x%08" PRIX32
                     " status_header=0x%08" PRIX32 " status=0x%08" PRIX32,
                     s_spi.tx[0], address, decode(s_spi.rx),
                     decode(s_spi.rx + 4), decode(s_spi.status_rx),
                     decode(s_spi.status_rx + 4));
            fail(ESP_ERR_INVALID_RESPONSE);
        }
    }
}

static void burst_start(uint8_t opcode, uint16_t address, uint8_t words) {
    if (!session_owned()) {
        return;
    }
    if (s_spi.burst) {
        fail(ESP_ERR_INVALID_STATE);
        return;
    }
    s_spi.count = words == 0 ? MAX_WORDS : words;
    s_spi.cursor = 0;
    s_spi.opcode = opcode;
    s_spi.burst = true;
    if (!valid_address(address, s_spi.count)) {
        fail(ESP_ERR_INVALID_ARG);
    }
    memset(s_spi.tx, 0, 4U + s_spi.count * 4U);
    memset(s_spi.rx, 0, 4U + s_spi.count * 4U);
    s_spi.tx[0] = opcode;
    s_spi.tx[1] = address >> 8;
    s_spi.tx[2] = address;
    s_spi.tx[3] = words;
    if (opcode == AHB_READ_OPCODE) {
        transfer();
    }
}

void AHB_WRITE_BURST_START(uint16_t address, uint8_t words) {
    burst_start(AHB_WRITE_OPCODE, address, words);
}

void AHB_WRITE_BURST_WRITE(uint32_t data) {
    if (!session_owned()) {
        return;
    }
    if (!s_spi.burst || s_spi.opcode != AHB_WRITE_OPCODE ||
        s_spi.cursor >= s_spi.count) {
        fail(ESP_ERR_INVALID_STATE);
        return;
    }
    encode(s_spi.tx + 4U + s_spi.cursor++ * 4U, data);
}

void AHB_WRITE_BURST_END(void) {
    if (!session_owned()) {
        return;
    }
    if (!s_spi.burst || s_spi.opcode != AHB_WRITE_OPCODE ||
        s_spi.cursor != s_spi.count) {
        fail(ESP_ERR_INVALID_STATE);
    } else {
        transfer();
    }
    s_spi.burst = false;
}

void AHB_READ_BURST_START(uint16_t address, uint8_t words) {
    burst_start(AHB_READ_OPCODE, address, words);
}

uint32_t AHB_READ_BURST_READ(void) {
    if (!session_owned()) {
        return 0;
    }
    if (!s_spi.burst || s_spi.opcode != AHB_READ_OPCODE ||
        s_spi.cursor >= s_spi.count) {
        fail(ESP_ERR_INVALID_STATE);
        return 0;
    }
    const uint32_t word = decode(s_spi.rx + 4U + s_spi.cursor++ * 4U);
    return s_spi.error == ESP_OK ? word : 0;
}

void AHB_READ_BURST_END(void) {
    if (!session_owned()) {
        return;
    }
    if (!s_spi.burst || s_spi.opcode != AHB_READ_OPCODE ||
        s_spi.cursor != s_spi.count) {
        fail(ESP_ERR_INVALID_STATE);
    }
    s_spi.burst = false;
}

void AHB_WRITE_32(uint16_t address, uint32_t data) {
    AHB_WRITE_BURST_START(address, 1);
    AHB_WRITE_BURST_WRITE(data);
    AHB_WRITE_BURST_END();
}

uint32_t AHB_READ_32(uint16_t address) {
    AHB_READ_BURST_START(address, 1);
    const uint32_t word = AHB_READ_BURST_READ();
    AHB_READ_BURST_END();
    return word;
}
