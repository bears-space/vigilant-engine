# Peripherals

Vigilant Engine includes drivers and integration for common peripherals used across Aerobear nodes.

## Supported peripherals

- **WS2812B**: Addressable RGB LEDs (status indication, effects)
- **TCAN4550-Q1**: SPI CAN controller **(NOT IMPLEMENTED!)**
- **TCAN332**: CAN transceiver **(NOT IMPLEMENTED!)**

These peripherals are wrapped behind a common abstraction layer to simplify reuse across multiple firmware targets.
