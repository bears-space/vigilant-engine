/*
# SPDX-AI-Disclosure: ai-generated
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: SPI-device mock and regression tests; tests are AI-generated.
# SPDX-AI-Date: 2026-10-15
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_idf.h"
#include "tcan4550.h"
#include "tcan4550_ti.h"

static uint32_t regs[0x8800U / 4U];
static unsigned heap_live, mutex_live, device_live, bus_live, transfers,
    acknowledgements;
static int fail_address = -1, spi_error_address = -1, alloc_fail_after = -1;
static bool complete_tx = true, complete_on_cancel, borrowed_bus;
static uintptr_t current_task = 1;
static TickType_t ticks;
struct mock_mutex {
    TaskHandle_t owner;
};

static uint32_t decode(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static void encode(uint8_t* p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}
void* heap_caps_calloc(size_t n, size_t size, unsigned caps) {
    (void)caps;
    if (alloc_fail_after == 0) return NULL;
    if (alloc_fail_after > 0) --alloc_fail_after;
    void* p = calloc(n, size);
    if (p) ++heap_live;
    return p;
}
void heap_caps_free(void* p) {
    if (p) {
        --heap_live;
        free(p);
    }
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    SemaphoreHandle_t m = calloc(1, sizeof(*m));
    if (m) ++mutex_live;
    return m;
}
void vSemaphoreDelete(SemaphoreHandle_t m) {
    --mutex_live;
    free(m);
}
int xSemaphoreTake(SemaphoreHandle_t m, TickType_t wait) {
    if (m->owner) {
        ticks += wait;
        return 0;
    }
    m->owner = xTaskGetCurrentTaskHandle();
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t m) {
    assert(m->owner == xTaskGetCurrentTaskHandle());
    m->owner = NULL;
    return pdTRUE;
}
TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t m) { return m->owner; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void*)current_task; }
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t n) { ticks += n; }
esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t* c,
                             int dma) {
    (void)host;
    (void)dma;
    assert(c->mosi_io_num == 11 && c->miso_io_num == 13 &&
           c->sclk_io_num == 12);
    assert(c->max_transfer_sz >= 1028);
    if (borrowed_bus) return ESP_ERR_INVALID_STATE;
    assert(bus_live == 0);
    ++bus_live;
    return ESP_OK;
}
esp_err_t spi_bus_add_device(spi_host_device_t host,
                             const spi_device_interface_config_t* c,
                             spi_device_handle_t* out) {
    (void)host;
    assert(c->mode == 0 && c->spics_io_num == 10 &&
           c->clock_speed_hz == 2000000);
    ++device_live;
    *out = (void*)1;
    return ESP_OK;
}
esp_err_t spi_bus_remove_device(spi_device_handle_t d) {
    assert(d);
    --device_live;
    return ESP_OK;
}
esp_err_t spi_bus_free(spi_host_device_t host) {
    (void)host;
    assert(!borrowed_bus);
    --bus_live;
    return ESP_OK;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t d,
                                      spi_transaction_t* t) {
    assert(d);
    const uint8_t* tx = t->tx_buffer;
    uint8_t* rx = t->rx_buffer;
    const unsigned address = ((unsigned)tx[1] << 8) | tx[2];
    const unsigned count = tx[3] ? tx[3] : 256;
    assert(t->length == (4U + count * 4U) * 8U);
    assert(address % 4 == 0 &&
           address / 4 + count <= sizeof(regs) / sizeof(regs[0]));
    ++transfers;
    if ((int)address == fail_address) {
        fail_address = -1;
        return ESP_FAIL;
    }
    memset(rx, 0, t->length / 8);
    rx[0] = regs[REG_SPI_STATUS / 4] ? 8 : 0;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned addr = address + 4 * i;
        if (tx[0] == AHB_READ_OPCODE) {
            encode(rx + 4 + i * 4, regs[addr / 4]);
        } else {
            assert(tx[0] == AHB_WRITE_OPCODE);
            uint32_t word = decode(tx + 4 + i * 4);
            if (addr == REG_SPI_STATUS || addr == REG_DEV_IR ||
                addr == REG_MCAN_IR) {
                regs[addr / 4] &= ~word;
            } else if (addr == REG_MCAN_RXF0A) {
                ++acknowledgements;
                regs[REG_MCAN_RXF0S / 4] = 0;
            } else if (addr == REG_MCAN_TXBAR) {
                regs[REG_MCAN_TXBTO / 4] = 0;
                regs[REG_MCAN_TXBRP / 4] = word;
                if (complete_tx) {
                    regs[REG_MCAN_TXBTO / 4] = word;
                    regs[REG_MCAN_TXBRP / 4] = 0;
                    if (regs[REG_MCAN_TEST / 4] & (1U << 4)) {
                        unsigned source =
                            REG_MRAM + (regs[REG_MCAN_TXBC / 4] & 0xFFFC);
                        memcpy(&regs[REG_MRAM / 4], &regs[source / 4], 72);
                        regs[REG_MCAN_RXF0S / 4] = 1;
                    }
                }
            } else if (addr == REG_MCAN_TXBCR) {
                if (complete_on_cancel) {
                    regs[REG_MCAN_TXBTO / 4] |=
                        regs[REG_MCAN_TXBRP / 4] & word;
                }
                regs[REG_MCAN_TXBRP / 4] &= ~word;
            } else if (addr == REG_MCAN_CCCR) {
                /* Standby holds M_CAN in initialization/clock stop, even
                 * when software requests INIT=0 and CSR=0. */
                if ((regs[REG_DEV_MODES_AND_PINS / 4] & 0xC0U) == 0x40U) {
                    word |= REG_BITS_MCAN_CCCR_INIT | REG_BITS_MCAN_CCCR_CSR |
                            REG_BITS_MCAN_CCCR_CSA;
                }
                regs[addr / 4] = word;
            } else {
                if (addr == REG_DEV_MODES_AND_PINS) {
                    if ((word & 0xC0U) == 0x80U) {
                        assert(regs[REG_MCAN_CCCR / 4] & (1U << 5));
                        regs[REG_MCAN_CCCR / 4] &=
                            ~(REG_BITS_MCAN_CCCR_INIT | REG_BITS_MCAN_CCCR_CCE |
                              REG_BITS_MCAN_CCCR_CSR | REG_BITS_MCAN_CCCR_CSA);
                    } else if ((word & 0xC0U) == 0x40U) {
                        regs[REG_MCAN_CCCR / 4] |= REG_BITS_MCAN_CCCR_INIT |
                            REG_BITS_MCAN_CCCR_CSR | REG_BITS_MCAN_CCCR_CSA;
                    }
                }
                regs[addr / 4] = word;
            }
        }
    }
    if ((int)address == spi_error_address) {
        spi_error_address = -1;
        regs[REG_SPI_STATUS / 4] = 1U << 29;
    }
    return ESP_OK;
}

static tcan4550_config_t config = {
    .spi_host = 1,
    .cs_io = 10,
    .mosi_io = 11,
    .miso_io = 13,
    .sclk_io = 12,
    .spi_clock_hz = 2000000,
    .oscillator_hz = 40000000,
    .nominal_bitrate = 500000,
    .data_bitrate = 2000000,
};
static void identity(void) {
    memset(regs, 0, sizeof(regs));
    regs[0] = 0x4E414354;
    regs[1] = 0x30353534;
    regs[REG_MCAN_ENDN / 4] = 0x87654321;
    regs[REG_DEV_MODES_AND_PINS / 4] = 0x60; /* power-on standby */
    regs[REG_MCAN_CCCR / 4] = REG_BITS_MCAN_CCCR_INIT |
        REG_BITS_MCAN_CCCR_CSR | REG_BITS_MCAN_CCCR_CSA;
}
static void no_resources(void) {
    assert(!heap_live && !mutex_live && !device_live && !bus_live);
}
static void queue_frame(uint8_t dlc, bool fd, bool remote) {
    regs[REG_MCAN_RXF0S / 4] = 1;
    regs[REG_MRAM / 4] = (0x123U << 18) | (remote ? 1U << 29 : 0);
    regs[REG_MRAM / 4 + 1] = (uint32_t)dlc << 16 | (fd ? 1U << 21 : 0);
    for (unsigned i = 0; i < 16; ++i) regs[REG_MRAM / 4 + 2 + i] = 0x44332211;
}

int main(void) {
    /* Every allocation failure and failed identity must release resources. */
    for (int i = 0; i < 4; ++i) {
        alloc_fail_after = i;
        assert(tcan4550_init(&config) == ESP_ERR_NO_MEM);
        no_resources();
    }
    alloc_fail_after = -1;
    for (int i = 0; i < 2; ++i) {
        memset(regs, 0, sizeof(regs));
        assert(tcan4550_init(&config) == ESP_ERR_NOT_FOUND);
        no_resources();
    }
    identity();
    assert(tcan4550_init(&config) == ESP_OK);
    assert((regs[REG_DEV_MODES_AND_PINS / 4] & 0xC0U) == 0x80U);
    assert(!(regs[REG_MCAN_CCCR / 4] & (REG_BITS_MCAN_CCCR_INIT |
        REG_BITS_MCAN_CCCR_CSR | REG_BITS_MCAN_CCCR_CSA)));
    assert(regs[REG_MCAN_CCCR / 4] & (1U << 5));      /* passive startup */
    assert(!(regs[REG_MCAN_RXF0C / 4] & (1U << 31))); /* blocking FIFO */
    assert((regs[REG_MCAN_TXBC / 4] & 0xFFFF) == 16U * 72U);
    assert(tcan4550_run_loopback_test(TCAN4550_LOOPBACK_INTERNAL, 0x123) ==
           ESP_OK);
    assert(tcan4550_run_loopback_test(TCAN4550_LOOPBACK_INTERNAL, 0x1234567) ==
           ESP_OK);
    assert(tcan4550_start(false) == ESP_OK);

    tcan4550_frame_t frame;
    for (uint8_t dlc = 0; dlc < 16; ++dlc) {
        queue_frame(dlc, false, false);
        assert(tcan4550_receive(&frame, 0) == ESP_OK);
        assert(frame.data_length == (dlc > 8 ? 8 : dlc));
        assert(frame.data_length_code == dlc);
    }
    const uint8_t lengths[] = {0, 1,  2,  3,  4,  5,  6,  7,
                               8, 12, 16, 20, 24, 32, 48, 64};
    for (uint8_t dlc = 0; dlc < 16; ++dlc) {
        queue_frame(dlc, true, false);
        assert(tcan4550_receive(&frame, 0) == ESP_OK);
        assert(frame.data_length == lengths[dlc]);
        if (frame.data_length >= 4)
            assert(frame.data[0] == 0x11 && frame.data[3] == 0x44);
    }
    queue_frame(8, false, true);
    assert(tcan4550_receive(&frame, 0) == ESP_OK && frame.remote &&
           frame.data_length == 0);
    assert(tcan4550_receive(&frame, 0) == ESP_ERR_TIMEOUT);
    ticks = UINT32_MAX - 1U;
    assert(tcan4550_receive(&frame, 20) == ESP_ERR_TIMEOUT);
    assert(ticks == 0); /* deadline arithmetic survives scheduler tick wrap */

    frame = (tcan4550_frame_t){.id = 0x321, .data_length = 0};
    assert(tcan4550_transmit(&frame, 0) == ESP_OK);
    const unsigned tx = (REG_MRAM + 16U * 72U) / 4;
    for (unsigned i = 2; i < 4; ++i) assert(regs[tx + i] == 0); /* ECC */
    frame = (tcan4550_frame_t){.id = 0x1234567,
                               .extended = true,
                               .fd_format = true,
                               .bit_rate_switch = true,
                               .data_length = 64};
    for (unsigned i = 0; i < 64; ++i) frame.data[i] = i;
    assert(tcan4550_transmit(&frame, 100) == ESP_OK);
    assert(regs[tx] == (0x1234567U | 1U << 30));
    assert(regs[tx + 2] == 0x03020100 && regs[tx + 17] == 0x3F3E3D3C);
    frame.id = 0x20000000;
    assert(tcan4550_transmit(&frame, 100) == ESP_ERR_INVALID_ARG);
    frame.id = 0x123;
    frame.data_length = 9;
    assert(tcan4550_transmit(&frame, 100) == ESP_ERR_INVALID_SIZE);
    frame.data_length = 16;
    complete_tx = false;
    assert(tcan4550_transmit(&frame, 10) == ESP_ERR_TIMEOUT);
    assert(regs[REG_MCAN_TXBRP / 4] == 0);
    /* A frame already on the bus can finish successfully while the host
     * requests cancellation. Report the actual hardware result. */
    complete_on_cancel = true;
    assert(tcan4550_transmit(&frame, 10) == ESP_OK);
    complete_on_cancel = false;
    complete_tx = true;
    assert(tcan4550_start(true) == ESP_OK);
    assert(tcan4550_transmit(&frame, 100) == ESP_ERR_INVALID_STATE);
    assert(tcan4550_start(false) == ESP_OK);

    /* Failed payload reads must not acknowledge/dequeue a frame. */
    queue_frame(8, false, false);
    unsigned ack_before = acknowledgements;
    fail_address = REG_MRAM;
    assert(tcan4550_receive(&frame, 0) == ESP_FAIL);
    assert(acknowledgements == ack_before);
    assert(tcan4550_transmit(&frame, 100) == ESP_ERR_INVALID_STATE);
    assert(tcan4550_start(false) == ESP_OK);
    queue_frame(8, false, false);
    ack_before = acknowledgements;
    spi_error_address = REG_MRAM;
    assert(tcan4550_receive(&frame, 0) == ESP_ERR_INVALID_RESPONSE);
    assert(acknowledgements == ack_before);
    assert(tcan4550_ti_begin(100) == ESP_OK);
    TCAN4x5x_Device_ClearSPIERR();
    assert(tcan4550_ti_end() == ESP_OK);
    assert(tcan4550_start(false) == ESP_OK);

    /* A failed mode transition must block TX until an explicit successful
     * start. */
    fail_address = REG_MCAN_TEST;
    assert(tcan4550_start(true) == ESP_FAIL);
    assert(tcan4550_transmit(&frame, 100) == ESP_ERR_INVALID_STATE);
    assert(tcan4550_start(false) == ESP_OK);

    /* Full-length bursts keep the header and all 256 words in one transaction.
     */
    assert(tcan4550_ti_begin(0) == ESP_OK);
    assert(tcan4550_ti_begin(0) == ESP_ERR_INVALID_STATE);
    current_task = 2;
    assert(tcan4550_ti_begin(0) == ESP_ERR_TIMEOUT);
    assert(tcan4550_ti_end() == ESP_ERR_INVALID_STATE);
    TickType_t before_wait = ticks;
    assert(tcan4550_receive(&frame, 10) == ESP_ERR_TIMEOUT);
    assert(ticks - before_wait == 1); /* mutex wait respects the RX deadline */
    current_task = 1;
    AHB_WRITE_BURST_START(REG_MRAM, 0);
    for (unsigned i = 0; i < 256; ++i) AHB_WRITE_BURST_WRITE(0xAABB0000U + i);
    AHB_WRITE_BURST_END();
    AHB_READ_BURST_START(REG_MRAM, 0);
    for (unsigned i = 0; i < 256; ++i)
        assert(AHB_READ_BURST_READ() == 0xAABB0000U + i);
    AHB_READ_BURST_END();
    assert(tcan4550_ti_end() == ESP_OK);

    assert(tcan4550_ti_begin(0) == ESP_OK);
    unsigned before_incomplete = transfers;
    AHB_WRITE_BURST_START(REG_MRAM, 2);
    AHB_WRITE_BURST_WRITE(0x12345678);
    AHB_WRITE_BURST_END();
    assert(tcan4550_ti_end() == ESP_ERR_INVALID_STATE);
    assert(transfers ==
           before_incomplete); /* incomplete writes never reach the bus */

    assert(tcan4550_ti_begin(0) == ESP_OK);
    TCAN4x5x_MRAM_Config oversized = {.Rx0NumElements = 64,
                                      .Rx0ElementSize = MRAM_64_Byte_Data};
    unsigned before = transfers;
    assert(!TCAN4x5x_MRAM_Configure(&oversized));
    assert(transfers == before);
    assert(tcan4550_ti_end() == ESP_OK);
    assert(tcan4550_ti_begin(0) == ESP_OK);
    before = transfers;
    AHB_WRITE_32(0x87FC, 0x11223344);
    AHB_READ_BURST_START(0x87FC, 2);
    AHB_READ_BURST_READ();
    AHB_READ_BURST_READ();
    AHB_READ_BURST_END();
    assert(tcan4550_ti_end() == ESP_ERR_INVALID_ARG && transfers == before + 2);
    fail_address = REG_DEV_MODES_AND_PINS;
    assert(tcan4550_deinit() == ESP_FAIL);
    assert(device_live == 1 && mutex_live == 1 && heap_live == 4);
    assert(tcan4550_deinit() == ESP_OK);
    no_resources();

    identity();
    config.data_bitrate = 500000;
    assert(tcan4550_init(&config) == ESP_OK);
    assert(((regs[REG_MCAN_DBTP / 4] >> 16) & 31) == 1);
    assert(((regs[REG_MCAN_TDCR / 4] >> 8) & 127) == 58);
    assert(tcan4550_deinit() == ESP_OK);
    no_resources();
    identity();
    config.nominal_bitrate = config.data_bitrate = 250000;
    borrowed_bus = true;
    assert(tcan4550_init(&config) == ESP_OK);
    assert(!(regs[REG_MCAN_DBTP / 4] & (1U << 23)));
    assert(tcan4550_deinit() == ESP_OK);
    no_resources();
    borrowed_bus = false;
    puts(
        "PASS: TI/ESP-IDF port framing, DLCs, loopback, timing, faults, "
        "sessions, ECC, cancellation, and cleanup");
    return 0;
}
