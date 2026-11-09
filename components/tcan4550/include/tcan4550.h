/*
# SPDX-AI-Disclosure: ai-assisted
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: Public API additions and documentation changes.
# SPDX-AI-Date: 2026-10-13
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** TCAN4550 SPI and CAN timing configuration. */
typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t cs_io;
    gpio_num_t mosi_io;
    gpio_num_t miso_io;
    gpio_num_t sclk_io;
    uint32_t spi_clock_hz;
    uint32_t oscillator_hz;
    uint32_t nominal_bitrate;
    uint32_t data_bitrate;
} tcan4550_config_t;

/** A received or transmitted Classical CAN / CAN-FD frame. */
typedef struct {
    uint32_t id;
    bool extended;
    bool remote;
    bool fd_format;
    bool bit_rate_switch;
    bool error_state_indicator;
    bool accepted_non_matching;
    bool rx_fifo_message_lost;
    uint8_t filter_index;
    uint16_t timestamp;
    uint8_t data_length_code;
    uint8_t data_length;
    uint8_t data[64];
} tcan4550_frame_t;

typedef enum {
    /** Tests the SPI interface, M_CAN core, and message RAM. */
    TCAN4550_LOOPBACK_INTERNAL,
    /** Internally loops TX to RX while also driving the CAN bus.
     * Does not verify the physical transceiver receive path. */
    TCAN4550_LOOPBACK_EXTERNAL,
} tcan4550_loopback_t;

typedef struct {
    uint32_t spi_status;
    uint32_t device_interrupts;
    uint32_t mcan_interrupts;
    uint32_t protocol_status;
    uint32_t error_counters;
} tcan4550_diagnostics_t;

/**
 * Initialize the singleton through the TI library and verify SPI reads/writes.
 * Starts in passive monitor mode; call start(false) for normal operation.
 * Init and deinit require exclusive ownership (stop all client tasks first).
 *
 * The oscillator must be either 20 MHz or 40 MHz. Bit timing is calculated
 * from oscillator_hz, nominal_bitrate, and data_bitrate.
 */
esp_err_t tcan4550_init(const tcan4550_config_t* config);

/** Enter standby and free SPI/mutex resources. Stop all client tasks first. */
esp_err_t tcan4550_deinit(void);

/**
 * Send and receive a known 16-byte CAN-FD beacon in the selected loopback mode.
 * beacon_id may be an 11-bit standard ID or a 29-bit extended ID.
 */
esp_err_t tcan4550_run_loopback_test(tcan4550_loopback_t loopback,
                                     uint32_t beacon_id);

/**
 * Leave loopback and start receiving from CANH/CANL.
 *
 * monitor_only=false is normal CAN operation and acknowledges valid incoming
 * frames. monitor_only=true is passive and does not acknowledge frames.
 */
esp_err_t tcan4550_start(bool monitor_only);

/** Transmit one frame and wait for completion. Rejected in passive monitor
 * mode. On timeout, cancellation is requested and completion checked for up to
 * 100 ms. A frame already on the bus may complete despite the cancellation
 * request. */
esp_err_t tcan4550_transmit(const tcan4550_frame_t* frame, uint32_t timeout_ms);

/** Receive the oldest FIFO frame, waiting for up to timeout_ms. */
esp_err_t tcan4550_receive(tcan4550_frame_t* frame, uint32_t timeout_ms);

/** Read raw device and M_CAN diagnostic registers. */
esp_err_t tcan4550_get_diagnostics(tcan4550_diagnostics_t* diagnostics);

/** Clear the selected write-one-to-clear M_CAN interrupt flags. */
esp_err_t tcan4550_clear_mcan_interrupts(uint32_t flags);

/** Clear the selected write-one-to-clear TCAN4550 device interrupt flags. */
esp_err_t tcan4550_clear_device_interrupts(uint32_t flags);

#ifdef __cplusplus
}
#endif
