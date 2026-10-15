<!--
# SPDX-AI-Disclosure: ai-assisted
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: Documentation updates and loopback behavior corrections.
# SPDX-AI-Date: 2026-10-13
-->

# Peripherals

Vigilant Engine includes drivers and integration for common peripherals used across STARSTREAK nodes.

## Supported peripherals

- **WS2812B**: Addressable RGB LEDs (status indication, effects)
- **TCAN4550-Q1**: SPI CAN/CAN-FD controller and transceiver

These peripherals are wrapped behind a common abstraction layer to simplify reuse across multiple firmware targets.

## I2C bus

Vigilant Engine also provides an optional shared I2C master interface for external sensors and peripherals.

For setup and API usage, see the [I2C Interface](./i2c-interface.md) page.

## TCAN4550-Q1 CAN-FD setup

The driver uses the TI TCAN4550 library with an ESP-IDF SPI backend. Source,
license notices, API details, and validation instructions are documented in
[`components/tcan4550/README.md`](https://github.com/bears-space/APOGEE-Mainboard/blob/main/components/tcan4550/README.md).

The main application initializes the controller and starts normal CAN operation
with `tcan4550_init()` and `tcan4550_start(false)` using these GPIO assignments:

| Signal | GPIO |
| --- | ---: |
| nCS | 10 |
| SDI / MOSI | 11 |
| SCLK | 12 |
| SDO / MISO | 13 |

The configuration uses SPI2 at 2 MHz, a 40 MHz oscillator, 500 kbit/s arbitration,
and 2 Mbit/s CAN-FD data. Adjust the pins and timing constants in `main/main.c`
to match the hardware and bus.

Initialization verifies SPI access and configures the controller and message RAM.
Normal operation acknowledges valid incoming frames. The application does not
run loopback tests, send example frames, or create a receive or diagnostic task.
Add application-specific transmit and receive handling with `tcan4550_transmit()`
and `tcan4550_receive()` after initialization. The RX FIFO holds 16 frames, so
receive handling must drain it regularly to avoid losing frames.

### Status LED

Depending on the mode, the status LED gives information about the status of the device differently.

Supported LEDs: Generic 1-Pin, Generic RGB, WS2812B

For more information about the available settings, see the menuconfig and the [config page](./config.md).

#### RGB-Mode
- **Green (slow) blinking**: Info, Period is 1s
- **Blue (faster) blinking**: Warning, Period is 600ms
- **Red (fast) blinking**: Error, Period is 300ms

#### Blink-Mode (Generic 1-Pin only)
- **Slow blinking**: Info, Period is 2s
- **Faster blinking**: Warning, Period is 700ms
- **Fast blinking**: Error, Period is 100ms
