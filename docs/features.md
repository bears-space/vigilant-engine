# Firmware Features

Vigilant Engine provides a common set of capabilities intended to be reused across STARSTREAK nodes.

## OTA updates

- HTTP/Web-based firmware updates
- Designed for safe, recoverable update flow

## Network modes

- `AP`: Device hosts its own access point
- `STA`: Device connects to an existing Wi-Fi network
- `APSTA`: Dual mode

## WiFi master mode

- Optional compile-time feature controlled by `VE_ENABLE_WIFI_MASTER`
- Probes AP clients for the Vigilant device magic
- Keeps non-Vigilant clients from being polled continuously

## Web server

- Configuration and status pages
- OTA firmware upload endpoint
- Endpoint structure intended to be extended per project

## Debugging and logging

- Wireless logging over network (websocket, visible on the dashboard)
- Centralized logging API for modules
- Status LED

## Recovery app
- Boots when the main app boot-loops
- Provides the OTA functionality
- WebUI for intuitive flashing
