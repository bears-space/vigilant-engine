# Peripherals

Vigilant Engine includes drivers and integration for common peripherals used across STARSTREAK nodes.

## Supported peripherals

- **WS2812B**: Addressable RGB LEDs (status indication, effects)
- **TCAN4550-Q1**: SPI CAN controller **(NOT IMPLEMENTED!)**
- **TCAN332**: CAN transceiver **(NOT IMPLEMENTED!)**

These peripherals are wrapped behind a common abstraction layer to simplify reuse across multiple firmware targets.

## I2C bus

Vigilant Engine also provides an optional shared I2C master interface for external sensors and peripherals.

For setup and API usage, see the [I2C Interface](./i2c-interface.md) page.

### Status LED

Supported LEDs: WS2812B


ESP-IDF info logs trigger a very short green pulse. Warnings trigger a short yellow pulse. The first ESP-IDF error log latches the LED into a red blink using the configured `VE_LOG_ERROR_PULSE_MS` duration, and later info or warning logs are ignored.

The colours of the log levels are configurable in the kconfig settings.

For more information about the available settings, see the menuconfig and the [config page](./config.md).
