# Firmware Features

Vigilant Engine provides a common set of capabilities intended to be reused across Aerobear nodes.

## OTA updates

- HTTP/Web-based firmware updates
- Designed for safe, recoverable update flow

## Network modes

- `AP`: Device hosts its own access point
- `STA`: Device connects to an existing Wi-Fi network
- `APSTA`: Dual mode

## Web server

- Configuration and status pages
- OTA firmware upload endpoint
- Endpoint structure intended to be extended per project

## Debugging and logging

- Wireless logging over network (websocket, visible on the dashboard)
- Centralized logging API for modules

## Recovery app
- Boots when the main app boot-loops
- Provides the OTA functionality
- WebUI for intuitive flashing