/*
# SPDX-AI-Disclosure: ai-assisted
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: ESP-IDF wrapper implementation, TI integration, and correctness fixes.
# SPDX-AI-Date: 2026-10-15
*/

#include "tcan4550.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tcan4550_spi.h"
#include "tcan4550_ti.h"

#define TCAN_REG_DEVICE_ID1 0x0000U
#define TCAN_REG_DEVICE_ID2 0x0004U
#define TCAN_REG_REVISION 0x0008U
#define TCAN_REG_SPI_STATUS 0x000CU
#define TCAN_REG_MODES 0x0800U
#define TCAN_REG_SCRATCH 0x0808U
#define TCAN_REG_DEVICE_IR 0x0820U

#define TCAN_REG_TEST 0x1010U
#define TCAN_REG_CCCR 0x1018U
#define TCAN_REG_ECR 0x1040U
#define TCAN_REG_PSR 0x1044U
#define TCAN_REG_IR 0x1050U
#define TCAN_REG_IE 0x1054U
#define TCAN_REG_ILS 0x1058U
#define TCAN_REG_ILE 0x105CU
#define TCAN_REG_XIDAM 0x1090U
#define TCAN_REG_RXF0S 0x10A4U
#define TCAN_REG_RXF0A 0x10A8U
#define TCAN_REG_TXBRP 0x10CCU
#define TCAN_REG_TXBCR 0x10D4U
#define TCAN_REG_TXBTO 0x10D8U

#define TCAN_RX_FIFO_ELEMENTS 16U

#define TCAN_DEVICE_ID1_VALUE 0x4E414354UL
#define TCAN_DEVICE_ID2_VALUE 0x30353534UL
#define TCAN_ENDIAN_VALUE 0x87654321UL
#define TCAN_REG_ENDN 0x1004U

#define TCAN_MODES_CLK_REF_40MHZ (1UL << 27)
#define TCAN_MODES_TEST_MODE_EN (1UL << 21)
#define TCAN_MODES_MODE_MASK (3UL << 6)
#define TCAN_MODES_MODE_NORMAL (2UL << 6)
#define TCAN_MODES_RESERVED_SET (1UL << 5)
#define TCAN_MODES_WATCHDOG_EN (1UL << 3)
#define TCAN_MODES_DEVICE_RESET (1UL << 2)
#define TCAN_MODES_TEST_CONFIG (1UL << 0)

#define TCAN_CCCR_BRSE (1UL << 9)
#define TCAN_CCCR_FDOE (1UL << 8)
#define TCAN_CCCR_TEST (1UL << 7)
#define TCAN_CCCR_MON (1UL << 5)
#define TCAN_CCCR_CSR (1UL << 4)
#define TCAN_CCCR_CSA (1UL << 3)
#define TCAN_CCCR_CCE (1UL << 1)
#define TCAN_CCCR_INIT (1UL << 0)
#define TCAN_TEST_LBCK (1UL << 4)

#define TCAN_RXF0S_LOST (1UL << 25)
#define TCAN_RXF0S_GET_INDEX_MASK (0x3FUL << 8)
#define TCAN_RXF0S_FILL_MASK 0x7FUL
#define TCAN_IR_RF0N (1UL << 0)
#define TCAN_IR_RF0L (1UL << 3)

#define TCAN_TX_BUFFER_0 (1UL << 0)
#define TCAN_SPI_ERROR_W1C_MASK 0x3F3F0000UL
#define TCAN_DEVICE_IR_W1C_MASK 0x00FDE500UL

typedef enum {
    TCAN_STATE_UNINITIALIZED,
    TCAN_STATE_FAULT,
    TCAN_STATE_INTERNAL_LOOPBACK,
    TCAN_STATE_EXTERNAL_LOOPBACK,
    TCAN_STATE_RUNNING,
} tcan_state_t;

typedef struct {
    tcan4550_config_t config;
    tcan_state_t state;
    bool monitor_only;
    uint32_t nbtp;
    uint32_t dbtp;
    uint32_t tdcr;
} tcan4550_context_t;

static const char* TAG = "tcan4550";
static tcan4550_context_t s_tcan;

static esp_err_t register_read(uint16_t address, uint32_t* value) {
    *value = AHB_READ_32(address);
    return tcan4550_spi_error();
}

static esp_err_t register_write(uint16_t address, uint32_t value) {
    AHB_WRITE_32(address, value);
    return tcan4550_spi_error();
}

static esp_err_t ti_result(bool result) {
    const esp_err_t err = tcan4550_spi_error();
    return err != ESP_OK ? err : (result ? ESP_OK : ESP_ERR_INVALID_RESPONSE);
}

static esp_err_t finish_session(esp_err_t err) {
    const esp_err_t transport_err = tcan4550_spi_error();
    if (transport_err != ESP_OK) {
        s_tcan.state = TCAN_STATE_FAULT;
    }
    const esp_err_t end_err = tcan4550_ti_end();
    return end_err != ESP_OK ? end_err : err;
}

static TickType_t timeout_ticks(uint32_t milliseconds) {
    const uint64_t ticks =
        ((uint64_t)milliseconds * configTICK_RATE_HZ + 999U) / 1000U;
    return ticks >= portMAX_DELAY ? portMAX_DELAY - 1U : (TickType_t)ticks;
}

static esp_err_t wait_for_register_bits(uint16_t address, uint32_t mask,
                                        uint32_t expected,
                                        uint32_t timeout_ms) {
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = timeout_ticks(timeout_ms);
    uint32_t value = 0;

    do {
        ESP_RETURN_ON_ERROR(register_read(address, &value), TAG,
                            "Failed to read register 0x%04X", address);
        if ((value & mask) == expected) {
            return ESP_OK;
        }
        if ((xTaskGetTickCount() - start) >= timeout) {
            break;
        }
        vTaskDelay(1);
    } while ((xTaskGetTickCount() - start) <= timeout);

    ESP_LOGE(TAG,
             "Register 0x%04X timeout: value=0x%08" PRIX32
             " mask=0x%08" PRIX32 " expected=0x%08" PRIX32,
             address, value, mask, expected);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t calculate_nominal_timing(uint32_t clock_hz, uint32_t bitrate,
                                          uint32_t* register_value) {
    if (clock_hz == 0 || bitrate == 0 || register_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint32_t prescaler = 1; prescaler <= 512; ++prescaler) {
        const uint64_t denominator = (uint64_t)bitrate * prescaler;
        if (denominator == 0 || ((uint64_t)clock_hz % denominator) != 0) {
            continue;
        }

        const uint32_t total_tq = (uint32_t)((uint64_t)clock_hz / denominator);
        const uint32_t before_sample = (total_tq * 80U + 50U) / 100U;
        if (before_sample < 3 || before_sample >= total_tq) {
            continue;
        }

        const uint32_t time_seg1 = before_sample - 1U;
        const uint32_t time_seg2 = total_tq - before_sample;
        if (time_seg1 < 2 || time_seg1 > 256 || time_seg2 < 2 ||
            time_seg2 > 128) {
            continue;
        }

        const uint32_t sync_jump_width = time_seg2;
        *register_value = ((sync_jump_width - 1U) << 25) |
                          ((prescaler - 1U) << 16) | ((time_seg1 - 1U) << 8) |
                          (time_seg2 - 1U);
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t calculate_data_timing(uint32_t clock_hz, uint32_t bitrate,
                                       uint32_t* dbtp, uint32_t* tdcr) {
    if (clock_hz == 0 || bitrate == 0 || dbtp == NULL || tdcr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint32_t prescaler = 1; prescaler <= 32; ++prescaler) {
        const uint64_t denominator = (uint64_t)bitrate * prescaler;
        if (denominator == 0 || ((uint64_t)clock_hz % denominator) != 0) {
            continue;
        }

        const uint32_t total_tq = (uint32_t)((uint64_t)clock_hz / denominator);
        const uint32_t before_sample = (total_tq * 75U + 50U) / 100U;
        if (before_sample < 2 || before_sample >= total_tq) {
            continue;
        }

        const uint32_t time_seg1 = before_sample - 1U;
        const uint32_t time_seg2 = total_tq - before_sample;
        if (time_seg1 < 1 || time_seg1 > 32 || time_seg2 < 1 ||
            time_seg2 > 16) {
            continue;
        }

        const uint32_t sync_jump_width = time_seg2;
        *dbtp = ((prescaler - 1U) << 16) | ((time_seg1 - 1U) << 8) |
                ((time_seg2 - 1U) << 4) | (sync_jump_width - 1U);
        /* TDCO uses CAN clock periods, not prescaled time quanta.
         * M_CAN TDC supports actual data prescalers 1 and 2 only. */
        *tdcr = 0;
        if (prescaler <= 2) {
            *dbtp |= 1UL << 23;
            *tdcr = ((before_sample - 1U) * prescaler) << 8;
        }
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t mcan_enter_initialization(void) {
    uint32_t cccr = 0;
    ESP_RETURN_ON_ERROR(register_read(TCAN_REG_CCCR, &cccr), TAG,
                        "Could not read CCCR");
    cccr &= ~(TCAN_CCCR_CSR | TCAN_CCCR_CSA);
    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_CCCR, cccr | TCAN_CCCR_INIT),
                        TAG, "Could not request M_CAN initialization");
    ESP_RETURN_ON_ERROR(wait_for_register_bits(TCAN_REG_CCCR, TCAN_CCCR_INIT,
                                               TCAN_CCCR_INIT, 100),
                        TAG, "M_CAN did not enter initialization");
    ESP_RETURN_ON_ERROR(
        register_write(TCAN_REG_CCCR, (cccr | TCAN_CCCR_INIT | TCAN_CCCR_CCE) &
                                          ~(TCAN_CCCR_CSR | TCAN_CCCR_CSA)),
        TAG, "Could not unlock protected M_CAN registers");
    return wait_for_register_bits(TCAN_REG_CCCR, TCAN_CCCR_INIT | TCAN_CCCR_CCE,
                                  TCAN_CCCR_INIT | TCAN_CCCR_CCE, 100);
}

static esp_err_t mcan_leave_initialization(uint32_t cccr_mode) {
    ESP_RETURN_ON_ERROR(
        register_write(TCAN_REG_CCCR,
                       cccr_mode & ~(TCAN_CCCR_CSR | TCAN_CCCR_CSA |
                                     TCAN_CCCR_CCE | TCAN_CCCR_INIT)),
        TAG, "Could not start M_CAN");
    return wait_for_register_bits(TCAN_REG_CCCR, TCAN_CCCR_INIT, 0, 100);
}

static esp_err_t configure_message_ram(void) {
    TCAN4x5x_MRAM_Clear();
    ESP_RETURN_ON_ERROR(tcan4550_spi_error(), TAG, "Message RAM clear failed");
    TCAN4x5x_MRAM_Config ram = {
        .Rx0NumElements = TCAN_RX_FIFO_ELEMENTS,
        .Rx0ElementSize = MRAM_64_Byte_Data,
        .TxBufferNumElements = 1,
        .TxBufferElementSize = MRAM_64_Byte_Data,
    };
    ESP_RETURN_ON_ERROR(ti_result(TCAN4x5x_MRAM_Configure(&ram)), TAG,
                        "TI message RAM configuration failed");
    TCAN4x5x_MCAN_Global_Filter_Configuration filter = {.word = 0};
    ESP_RETURN_ON_ERROR(ti_result(TCAN4x5x_MCAN_ConfigureGlobalFilter(&filter)),
                        TAG, "TI global filter configuration failed");
    return register_write(TCAN_REG_XIDAM, 0x1FFFFFFFUL);
}

static esp_err_t configure_mcan(void) {
    ESP_RETURN_ON_ERROR(mcan_enter_initialization(), TAG,
                        "Could not enter M_CAN initialization");

    TCAN4x5x_MCAN_CCCR_Config control = {
        .word = TCAN_CCCR_FDOE | TCAN_CCCR_BRSE | TCAN_CCCR_MON,
    };
    ESP_RETURN_ON_ERROR(
        ti_result(TCAN4x5x_MCAN_ConfigureCCCRRegister(&control)), TAG,
        "TI CAN-FD control configuration failed");
    TCAN4x5x_MCAN_Nominal_Timing_Raw nominal = {
        .NominalBitRatePrescaler = (s_tcan.nbtp >> 16) & 0x1FF,
        .NominalTimeSeg1andProp = (s_tcan.nbtp >> 8) & 0xFF,
        .NominalTimeSeg2 = s_tcan.nbtp & 0x7F,
        .NominalSyncJumpWidth = (s_tcan.nbtp >> 25) & 0x7F,
    };
    ESP_RETURN_ON_ERROR(
        ti_result(TCAN4x5x_MCAN_ConfigureNominalTiming_Raw(&nominal)), TAG,
        "TI nominal timing configuration failed");
    TCAN4x5x_MCAN_Data_Timing_Raw data = {
        .DataBitRatePrescaler = (s_tcan.dbtp >> 16) & 0x1F,
        .DataTimeSeg1andProp = (s_tcan.dbtp >> 8) & 0x1F,
        .DataTimeSeg2 = (s_tcan.dbtp >> 4) & 0xF,
        .DataSyncJumpWidth = s_tcan.dbtp & 0xF,
        .TDCOffset = (s_tcan.tdcr >> 8) & 0x7F,
    };
    ESP_RETURN_ON_ERROR(ti_result(TCAN4x5x_MCAN_ConfigureDataTiming_Raw(&data)),
                        TAG, "TI data timing configuration failed");
    ESP_RETURN_ON_ERROR(configure_message_ram(), TAG,
                        "Message RAM configuration failed");

    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_IE, 0), TAG,
                        "M_CAN interrupt disable failed");
    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_ILS, 0), TAG,
                        "M_CAN interrupt routing failed");
    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_ILE, 0), TAG,
                        "M_CAN interrupt output disable failed");
    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_IR, UINT32_MAX), TAG,
                        "M_CAN interrupt clear failed");
    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_TEST, 0), TAG,
                        "M_CAN test mode clear failed");

    /* Standby holds M_CAN in clock stop. Enter normal device mode only
     * after timing/RAM and MON are configured, before waiting for INIT=0.
     * The device mode transition itself also requests INIT=0. */
    ESP_RETURN_ON_ERROR(
        ti_result(TCAN4x5x_Device_SetMode(TCAN4x5x_DEVICE_MODE_NORMAL)), TAG,
        "TI normal device mode failed");
    ESP_RETURN_ON_ERROR(
        wait_for_register_bits(TCAN_REG_MODES, TCAN_MODES_MODE_MASK,
                               TCAN_MODES_MODE_NORMAL, 100),
        TAG, "Device did not enter normal mode");
    ESP_RETURN_ON_ERROR(mcan_leave_initialization(
                            TCAN_CCCR_FDOE | TCAN_CCCR_BRSE | TCAN_CCCR_MON),
                        TAG, "Could not start configured M_CAN");
    s_tcan.state = TCAN_STATE_RUNNING;
    s_tcan.monitor_only = true;
    return ESP_OK;
}

static esp_err_t configure_device_mode(void) {
    /* Keep the transceiver off the bus until timing/RAM and MON are configured.
     */
    ESP_RETURN_ON_ERROR(
        ti_result(TCAN4x5x_Device_SetMode(TCAN4x5x_DEVICE_MODE_STANDBY)), TAG,
        "TI standby device mode failed");
    TCAN4x5x_Device_Interrupts interrupts = {0};
    TCAN4x5x_Device_ReadInterrupts(&interrupts);
    interrupts.word &= TCAN_DEVICE_IR_W1C_MASK;
    TCAN4x5x_Device_ClearInterrupts(&interrupts);
    ESP_RETURN_ON_ERROR(tcan4550_spi_error(), TAG,
                        "TI device interrupt clear failed");
    TCAN4x5x_DEV_CONFIG config = {0};
    TCAN4x5x_Device_ReadConfig(&config);
    ESP_RETURN_ON_ERROR(tcan4550_spi_error(), TAG,
                        "TI device configuration read failed");
    config.word &= ~(TCAN_MODES_CLK_REF_40MHZ | TCAN_MODES_TEST_MODE_EN |
                     TCAN_MODES_WATCHDOG_EN | TCAN_MODES_DEVICE_RESET |
                     TCAN_MODES_TEST_CONFIG);
    config.word |= TCAN_MODES_RESERVED_SET;
    if (s_tcan.config.oscillator_hz == 40000000U) {
        config.word |= TCAN_MODES_CLK_REF_40MHZ;
    }
    ESP_RETURN_ON_ERROR(ti_result(TCAN4x5x_Device_Configure(&config)), TAG,
                        "TI device configuration failed");
    return ESP_OK;
}

static esp_err_t discard_received_frames(void) {
    for (uint32_t count = 0; count < TCAN_RX_FIFO_ELEMENTS; ++count) {
        uint32_t status = 0;
        ESP_RETURN_ON_ERROR(register_read(TCAN_REG_RXF0S, &status), TAG,
                            "Could not read RX FIFO status");
        if ((status & TCAN_RXF0S_FILL_MASK) == 0) {
            return ESP_OK;
        }
        const uint32_t get_index = (status & TCAN_RXF0S_GET_INDEX_MASK) >> 8;
        ESP_RETURN_ON_ERROR(register_write(TCAN_REG_RXF0A, get_index), TAG,
                            "Could not acknowledge RX FIFO");
    }
    return ESP_OK;
}

static esp_err_t set_mcan_mode(tcan_state_t state, bool monitor_only) {
    s_tcan.state = TCAN_STATE_FAULT;
    ESP_RETURN_ON_ERROR(mcan_enter_initialization(), TAG,
                        "Could not stop M_CAN to change mode");

    uint32_t cccr =
        TCAN_CCCR_INIT | TCAN_CCCR_CCE | TCAN_CCCR_FDOE | TCAN_CCCR_BRSE;
    uint32_t test = 0;
    if (state == TCAN_STATE_INTERNAL_LOOPBACK) {
        cccr |= TCAN_CCCR_TEST | TCAN_CCCR_MON;
        test = TCAN_TEST_LBCK;
    } else if (state == TCAN_STATE_EXTERNAL_LOOPBACK) {
        cccr |= TCAN_CCCR_TEST;
        test = TCAN_TEST_LBCK;
    } else if (monitor_only) {
        cccr |= TCAN_CCCR_MON;
    }

    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_CCCR, cccr), TAG,
                        "Could not configure M_CAN mode");
    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_TEST, test), TAG,
                        "Could not configure M_CAN loopback");
    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_IR, UINT32_MAX), TAG,
                        "Could not clear M_CAN flags");
    ESP_RETURN_ON_ERROR(discard_received_frames(), TAG,
                        "Could not empty RX FIFO");
    ESP_RETURN_ON_ERROR(mcan_leave_initialization(cccr), TAG,
                        "Could not activate M_CAN mode");
    s_tcan.state = state;
    s_tcan.monitor_only = monitor_only;
    return ESP_OK;
}

static esp_err_t length_to_dlc(uint8_t length, uint8_t* dlc) {
    if (dlc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (length <= 8) {
        *dlc = length;
        return ESP_OK;
    }

    switch (length) {
        case 12:
            *dlc = 9;
            return ESP_OK;
        case 16:
            *dlc = 10;
            return ESP_OK;
        case 20:
            *dlc = 11;
            return ESP_OK;
        case 24:
            *dlc = 12;
            return ESP_OK;
        case 32:
            *dlc = 13;
            return ESP_OK;
        case 48:
            *dlc = 14;
            return ESP_OK;
        case 64:
            *dlc = 15;
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_SIZE;
    }
}

static esp_err_t validate_frame(const tcan4550_frame_t* frame, uint8_t* dlc) {
    if (frame == NULL || dlc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((frame->extended && frame->id > 0x1FFFFFFFUL) ||
        (!frame->extended && frame->id > 0x7FFU)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!frame->fd_format &&
        (frame->data_length > 8 || frame->bit_rate_switch)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (frame->fd_format && frame->remote) {
        return ESP_ERR_INVALID_ARG;
    }
    return length_to_dlc(frame->data_length, dlc);
}

static esp_err_t transmit_locked(const tcan4550_frame_t* frame,
                                 uint32_t timeout_ms) {
    uint8_t dlc = 0;
    ESP_RETURN_ON_ERROR(validate_frame(frame, &dlc), TAG, "Invalid CAN frame");

    uint32_t pending = 0;
    ESP_RETURN_ON_ERROR(register_read(TCAN_REG_TXBRP, &pending), TAG,
                        "Could not read TX pending state");
    if ((pending & TCAN_TX_BUFFER_0) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    TCAN4x5x_MCAN_TX_Header header = {
        .ID = frame->id,
        .XTD = frame->extended,
        .RTR = frame->remote,
        .ESI = frame->error_state_indicator,
        .DLC = dlc,
        .BRS = frame->bit_rate_switch,
        .FDF = frame->fd_format,
    };
    uint8_t payload[64] = {0};
    memcpy(payload, frame->data, frame->data_length);
    const uint32_t written = TCAN4x5x_MCAN_WriteTXBuffer(0, &header, payload);
    ESP_RETURN_ON_ERROR(ti_result(written == TCAN_TX_BUFFER_0), TAG,
                        "TI TX message RAM write failed");
    ESP_RETURN_ON_ERROR(ti_result(TCAN4x5x_MCAN_TransmitBufferContents(0)), TAG,
                        "TI transmission request failed");

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = timeout_ticks(timeout_ms);
    do {
        uint32_t transmitted = 0;
        ESP_RETURN_ON_ERROR(register_read(TCAN_REG_TXBTO, &transmitted), TAG,
                            "Could not read TX completion");
        if ((transmitted & TCAN_TX_BUFFER_0) != 0) {
            return ESP_OK;
        }
        if ((xTaskGetTickCount() - start) >= timeout) {
            break;
        }
        vTaskDelay(1);
    } while ((xTaskGetTickCount() - start) <= timeout);

    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_TXBCR, TCAN_TX_BUFFER_0), TAG,
                        "TX cancellation request failed");
    esp_err_t cancelled =
        wait_for_register_bits(TCAN_REG_TXBRP, TCAN_TX_BUFFER_0, 0, 100);
    if (cancelled != ESP_OK) {
        s_tcan.state = TCAN_STATE_FAULT;
        return cancelled;
    }
    /* Cancellation cannot stop a frame that has already completed on the
     * bus. Check the final result after the pending bit clears. */
    uint32_t transmitted = 0;
    ESP_RETURN_ON_ERROR(register_read(TCAN_REG_TXBTO, &transmitted), TAG,
                        "Could not read final TX completion");
    return (transmitted & TCAN_TX_BUFFER_0) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t receive_locked(tcan4550_frame_t* frame) {
    uint32_t fifo_status = 0;
    ESP_RETURN_ON_ERROR(register_read(TCAN_REG_RXF0S, &fifo_status), TAG,
                        "Could not read RX FIFO status");
    if ((fifo_status & TCAN_RXF0S_FILL_MASK) == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const uint32_t get_index = (fifo_status & TCAN_RXF0S_GET_INDEX_MASK) >> 8;
    if (get_index >= TCAN_RX_FIFO_ELEMENTS ||
        (fifo_status & TCAN_RXF0S_FILL_MASK) > TCAN_RX_FIFO_ELEMENTS) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    TCAN4x5x_MCAN_RX_Header header = {0};
    uint8_t payload[64] = {0};
    const uint8_t received =
        TCAN4x5x_MCAN_ReadNextFIFO(RXFIFO0, &header, payload);
    ESP_RETURN_ON_ERROR(tcan4550_spi_error(), TAG, "TI RX FIFO read failed");
    memset(frame, 0, sizeof(*frame));
    frame->id = header.ID;
    frame->extended = header.XTD;
    frame->remote = header.RTR;
    frame->error_state_indicator = header.ESI;
    frame->timestamp = header.RXTS;
    frame->data_length_code = header.DLC;
    frame->bit_rate_switch = header.BRS;
    frame->fd_format = header.FDF;
    frame->filter_index = header.FIDX;
    frame->accepted_non_matching = header.ANMF;
    frame->data_length = received;
    frame->rx_fifo_message_lost = (fifo_status & TCAN_RXF0S_LOST) != 0;
    memcpy(frame->data, payload, received);
    uint32_t clear_flags = TCAN_IR_RF0N;
    if ((fifo_status & TCAN_RXF0S_LOST) != 0) {
        clear_flags |= TCAN_IR_RF0L;
    }
    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_IR, clear_flags), TAG,
                        "Could not clear RX flags");
    return ESP_OK;
}

static esp_err_t initialize_device(void) {
    /* Clear a stale transport fault before checking identity. */
    TCAN4x5x_Device_ClearSPIERR();
    ESP_RETURN_ON_ERROR(tcan4550_spi_error(), TAG, "TI SPI error clear failed");
    uint32_t id1 = 0;
    uint32_t id2 = 0;
    uint32_t revision = 0;
    uint32_t endian = 0;
    ESP_RETURN_ON_ERROR(register_read(TCAN_REG_DEVICE_ID1, &id1), TAG,
                        "Device ID1 read failed");
    ESP_RETURN_ON_ERROR(register_read(TCAN_REG_DEVICE_ID2, &id2), TAG,
                        "Device ID2 read failed");
    ESP_RETURN_ON_ERROR(register_read(TCAN_REG_REVISION, &revision), TAG,
                        "Revision read failed");
    ESP_RETURN_ON_ERROR(register_read(TCAN_REG_ENDN, &endian), TAG,
                        "Endian register read failed");
    if (id1 != TCAN_DEVICE_ID1_VALUE || id2 != TCAN_DEVICE_ID2_VALUE ||
        endian != TCAN_ENDIAN_VALUE) {
        ESP_LOGE(TAG,
                 "Unexpected identity: ID1=0x%08" PRIX32 " ID2=0x%08" PRIX32
                 " ENDN=0x%08" PRIX32,
                 id1, id2, endian);
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t old_scratch = 0;
    uint32_t scratch = 0;
    ESP_RETURN_ON_ERROR(register_read(TCAN_REG_SCRATCH, &old_scratch), TAG,
                        "Scratch register read failed");
    ESP_RETURN_ON_ERROR(register_write(TCAN_REG_SCRATCH, 0xA5C35A3CUL), TAG,
                        "Scratch register write failed");
    ESP_RETURN_ON_ERROR(register_read(TCAN_REG_SCRATCH, &scratch), TAG,
                        "Scratch register verification read failed");
    (void)register_write(TCAN_REG_SCRATCH, old_scratch);
    if (scratch != 0xA5C35A3CUL) {
        ESP_LOGE(TAG, "SPI write verification failed: read 0x%08" PRIX32,
                 scratch);
        return ESP_FAIL;
    }
    ESP_RETURN_ON_ERROR(
        register_write(TCAN_REG_SPI_STATUS, TCAN_SPI_ERROR_W1C_MASK), TAG,
        "SPI error clear failed");
    ESP_RETURN_ON_ERROR(configure_device_mode(), TAG,
                        "TCAN4550 device configuration failed");
    ESP_RETURN_ON_ERROR(configure_mcan(), TAG, "M_CAN configuration failed");

    ESP_LOGI(TAG,
             "SPI verified: TCAN4550 revision=0x%08" PRIX32
             " (TI library / ESP-IDF port)",
             revision);
    ESP_LOGI(TAG,
             "CAN timing: oscillator=%" PRIu32 " Hz nominal=%" PRIu32
             " bit/s data=%" PRIu32 " bit/s NBTP=0x%08" PRIX32
             " DBTP=0x%08" PRIX32,
             s_tcan.config.oscillator_hz, s_tcan.config.nominal_bitrate,
             s_tcan.config.data_bitrate, s_tcan.nbtp, s_tcan.dbtp);
    return ESP_OK;
}

esp_err_t tcan4550_init(const tcan4550_config_t* config) {
    if (config == NULL || config->spi_clock_hz == 0 ||
        config->spi_clock_hz > 18000000U ||
        (config->oscillator_hz != 20000000U &&
         config->oscillator_hz != 40000000U) ||
        config->nominal_bitrate == 0 || config->nominal_bitrate > 1000000U ||
        config->data_bitrate < config->nominal_bitrate ||
        config->data_bitrate > 8000000U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_tcan.state != TCAN_STATE_UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_tcan, 0, sizeof(s_tcan));
    s_tcan.config = *config;
    ESP_RETURN_ON_ERROR(
        calculate_nominal_timing(config->oscillator_hz, config->nominal_bitrate,
                                 &s_tcan.nbtp),
        TAG, "Invalid nominal timing");
    ESP_RETURN_ON_ERROR(
        calculate_data_timing(config->oscillator_hz, config->data_bitrate,
                              &s_tcan.dbtp, &s_tcan.tdcr),
        TAG, "Invalid data timing");
    ESP_RETURN_ON_ERROR(tcan4550_spi_init(config), TAG,
                        "SPI transport initialization failed");
    esp_err_t err = tcan4550_ti_begin(1000);
    if (err == ESP_OK) {
        err = finish_session(initialize_device());
    }
    if (err != ESP_OK) {
        /* Best effort standby using a fresh session, then release owned
         * resources. */
        if (tcan4550_ti_begin(100) == ESP_OK) {
            (void)TCAN4x5x_Device_SetMode(TCAN4x5x_DEVICE_MODE_STANDBY);
            (void)tcan4550_ti_end();
        }
        esp_err_t cleanup = tcan4550_spi_deinit();
        s_tcan.state =
            cleanup == ESP_OK ? TCAN_STATE_UNINITIALIZED : TCAN_STATE_FAULT;
    }
    return err;
}

esp_err_t tcan4550_deinit(void) {
    if (s_tcan.state == TCAN_STATE_UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(tcan4550_ti_begin(1000), TAG,
                        "Could not lock device for shutdown");
    esp_err_t err =
        ti_result(TCAN4x5x_Device_SetMode(TCAN4x5x_DEVICE_MODE_STANDBY));
    err = finish_session(err);
    if (err != ESP_OK) {
        /* Retain access to the device so standby can be retried safely. */
        s_tcan.state = TCAN_STATE_FAULT;
        return err;
    }
    const esp_err_t cleanup = tcan4550_spi_deinit();
    s_tcan.state =
        cleanup == ESP_OK ? TCAN_STATE_UNINITIALIZED : TCAN_STATE_FAULT;
    return cleanup != ESP_OK ? cleanup : err;
}

esp_err_t tcan4550_run_loopback_test(tcan4550_loopback_t loopback,
                                     uint32_t beacon_id) {
    if (beacon_id > 0x1FFFFFFFUL || (loopback != TCAN4550_LOOPBACK_INTERNAL &&
                                     loopback != TCAN4550_LOOPBACK_EXTERNAL)) {
        return ESP_ERR_INVALID_ARG;
    }

    const tcan_state_t state = loopback == TCAN4550_LOOPBACK_INTERNAL
                                   ? TCAN_STATE_INTERNAL_LOOPBACK
                                   : TCAN_STATE_EXTERNAL_LOOPBACK;
    ESP_RETURN_ON_ERROR(tcan4550_ti_begin(1000), TAG,
                        "Could not lock TCAN4550");

    esp_err_t err = set_mcan_mode(state, false);
    tcan4550_frame_t sent = {
        .id = beacon_id,
        .extended = beacon_id > 0x7FFU,
        .fd_format = true,
        .bit_rate_switch = true,
        .data_length = 16,
        .data = {'T', 'C', 'A', 'N', '4', '5', '5', '0', 'S', 'E', 'L', 'F',
                 'T', 'E', 'S', 'T'},
    };
    if (err == ESP_OK) {
        sent.data[8] = loopback == TCAN4550_LOOPBACK_INTERNAL ? 'I' : 'E';
        err = transmit_locked(&sent, 250);
    }

    tcan4550_frame_t received = {0};
    if (err == ESP_OK) {
        const TickType_t start = xTaskGetTickCount();
        do {
            err = receive_locked(&received);
            if (err == ESP_OK) {
                break;
            }
            if (err != ESP_ERR_NOT_FOUND) {
                break;
            }
            vTaskDelay(1);
        } while ((xTaskGetTickCount() - start) <= pdMS_TO_TICKS(250));
        if (err == ESP_ERR_NOT_FOUND) {
            err = ESP_ERR_TIMEOUT;
        }
    }

    if (err == ESP_OK &&
        (received.id != sent.id || received.extended != sent.extended ||
         !received.fd_format || !received.bit_rate_switch ||
         received.data_length != sent.data_length ||
         memcmp(received.data, sent.data, sent.data_length) != 0)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    err = finish_session(err);

    if (err == ESP_OK) {
        ESP_LOGI(
            TAG, "%s loopback passed with CAN ID 0x%08" PRIX32,
            loopback == TCAN4550_LOOPBACK_INTERNAL ? "Internal" : "External",
            beacon_id);
    }
    return err;
}

esp_err_t tcan4550_start(bool monitor_only) {
    ESP_RETURN_ON_ERROR(tcan4550_ti_begin(1000), TAG,
                        "Could not lock TCAN4550");
    esp_err_t err = set_mcan_mode(TCAN_STATE_RUNNING, monitor_only);
    err = finish_session(err);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "CAN receiver started in %s mode",
                 monitor_only ? "passive monitor" : "normal");
    }
    return err;
}

esp_err_t tcan4550_transmit(const tcan4550_frame_t* frame,
                            uint32_t timeout_ms) {
    ESP_RETURN_ON_ERROR(tcan4550_ti_begin(timeout_ms), TAG,
                        "Could not lock TCAN4550");
    if (s_tcan.state == TCAN_STATE_FAULT ||
        (s_tcan.state == TCAN_STATE_RUNNING && s_tcan.monitor_only)) {
        return finish_session(ESP_ERR_INVALID_STATE);
    }
    esp_err_t err = transmit_locked(frame, timeout_ms);
    err = finish_session(err);
    return err;
}

esp_err_t tcan4550_receive(tcan4550_frame_t* frame, uint32_t timeout_ms) {
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = timeout_ticks(timeout_ms);
    do {
        const TickType_t elapsed = xTaskGetTickCount() - start;
        const uint64_t remaining_ms =
            elapsed >= timeout
                ? 0
                : (uint64_t)(timeout - elapsed) * 1000U / configTICK_RATE_HZ;
        ESP_RETURN_ON_ERROR(
            tcan4550_ti_begin(remaining_ms < 100 ? remaining_ms : 100), TAG,
            "Could not lock TCAN4550");
        if (s_tcan.state == TCAN_STATE_FAULT) {
            return finish_session(ESP_ERR_INVALID_STATE);
        }
        esp_err_t err = receive_locked(frame);
        err = finish_session(err);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        if (err != ESP_ERR_NOT_FOUND) {
            return err;
        }
        if ((xTaskGetTickCount() - start) >= timeout) {
            break;
        }
        /*
         * Do not convert a sub-tick delay with pdMS_TO_TICKS(): at 100 Hz,
         * 2 ms rounds down to zero and this polling loop starves the idle task.
         */
        vTaskDelay(1);
    } while ((xTaskGetTickCount() - start) <= timeout);

    return ESP_ERR_TIMEOUT;
}

esp_err_t tcan4550_get_diagnostics(tcan4550_diagnostics_t* diagnostics) {
    if (diagnostics == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(tcan4550_ti_begin(100), TAG, "Could not lock TCAN4550");
    memset(diagnostics, 0, sizeof(*diagnostics));

    esp_err_t err =
        register_read(TCAN_REG_SPI_STATUS, &diagnostics->spi_status);
    if (err == ESP_OK) {
        err =
            register_read(TCAN_REG_DEVICE_IR, &diagnostics->device_interrupts);
    }
    if (err == ESP_OK) {
        err = register_read(TCAN_REG_IR, &diagnostics->mcan_interrupts);
    }
    if (err == ESP_OK) {
        err = register_read(TCAN_REG_PSR, &diagnostics->protocol_status);
    }
    if (err == ESP_OK) {
        err = register_read(TCAN_REG_ECR, &diagnostics->error_counters);
    }
    err = finish_session(err);
    return err;
}

esp_err_t tcan4550_clear_mcan_interrupts(uint32_t flags) {
    ESP_RETURN_ON_ERROR(tcan4550_ti_begin(100), TAG, "Could not lock TCAN4550");
    esp_err_t err = register_write(TCAN_REG_IR, flags);
    err = finish_session(err);
    return err;
}

esp_err_t tcan4550_clear_device_interrupts(uint32_t flags) {
    ESP_RETURN_ON_ERROR(tcan4550_ti_begin(100), TAG, "Could not lock TCAN4550");
    esp_err_t err =
        register_write(TCAN_REG_DEVICE_IR, flags & TCAN_DEVICE_IR_W1C_MASK);
    err = finish_session(err);
    return err;
}
