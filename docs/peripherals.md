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
