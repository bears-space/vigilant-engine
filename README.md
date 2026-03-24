# Vigilant Engine

Vigilant Engine is the framework on which all ESP-based nodes of the **Aerobear** project are built.  
It provides the fundamental building blocks for connected, remotely maintainable devices, including
reliable OTA updates, flexible network modes, and peripheral support.

# Project Overview

## Core Features

- **OTA (Over-The-Air updates)**
  - Firmware updates via HTTP/Web UI
  - Designed for safe, recoverable updates on ESP targets

- **Network Modes**
  - `AP` – Device creates its own access point
  - `STA` – Device connects to an existing Wi-Fi network
  - `APSTA` – Dual mode (access point + station)

- **Webserver**
  - Serves configuration & status pages
  - Handles OTA firmware upload
  - Endpoint structure designed to be extendable per project

- **Debugging**
  - Wireless debugging over the network (e.g. TCP terminal)
  - Centralized logging API for modules
  - Optional verbose / debug builds

## Peripherals

Vigilant Engine includes drivers / integration for:

- **WS2812B** – Addressable RGB LEDs (status indication, effects)
- **TCAN4550-Q1** – SPI CAN controller
- **TCAN332** – CAN transceiver

These peripherals are wrapped in a common abstraction layer so they can be reused across all Aerobear nodes.

## Code Doumentation setup

This project is documented using MkDocs. For full documentation visit [mkdocs.org](https://www.mkdocs.org). Install MkDocs using pip into a venv.

### Commands

* `./.venv/bin/mkdocs serve` - Start the live-reloading docs server.
* `./.venv/bin/mkdocs build` - Build the documentation site.
* `./.venv/bin/mkdocs -h` - Print help message and exit.

### Project layout
    mkdocs.yml    # The configuration file.
    docs/
        index.md  # The documentation homepage.
        ...       # Other markdown pages, images and other files.
