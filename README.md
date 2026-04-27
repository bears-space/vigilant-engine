# Vigilant Engine

[![Frontend and Firmware CI](https://github.com/bears-space/vigilant-engine/actions/workflows/esp-build.yml/badge.svg)](https://github.com/bears-space/vigilant-engine/actions/workflows/esp-build.yml)
[![Docs](https://github.com/bears-space/vigilant-engine/actions/workflows/docs.yml/badge.svg)](https://github.com/bears-space/vigilant-engine/actions/workflows/docs.yml)
[![pre-commit](https://github.com/bears-space/vigilant-engine/actions/workflows/pre-commit.yaml/badge.svg)](https://github.com/bears-space/vigilant-engine/actions/workflows/pre-commit.yaml)
[![Ruff](https://github.com/bears-space/vigilant-engine/actions/workflows/ruff.yaml/badge.svg)](https://github.com/bears-space/vigilant-engine/actions/workflows/ruff.yaml)

Vigilant Engine is the framework on which all ESP-based nodes of the **STARSTREAK** project are built.
It provides the fundamental building blocks for connected, remotely maintainable devices, including
reliable OTA updates, flexible network modes, and peripheral support.

## Dev environment setup

Development should be done in VSCode using the ESP-IDF extension.

The project uses `uv` ([see here](https://docs.astral.sh/uv/getting-started/installation/)) for Python version and dependency management. To set this up, run:

```sh
uv sync
```

`pre-commit` is used to enforce style guides and catch common errors before they are committed. After running `uv sync`, run the following to set this up:

```sh
uv run pre-commit install
```

## Project Overview

### Core Features

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

### Peripherals

Vigilant Engine includes drivers / integration for:

- **WS2812B** – Addressable RGB LEDs (status indication, effects)
- **TCAN4550-Q1** – SPI CAN controller
- **TCAN332** – CAN transceiver

These peripherals are wrapped in a common abstraction layer so they can be reused across all STARSTREAK nodes.

### Code Doumentation setup

This project is documented using MkDocs. For full documentation visit [mkdocs.org](https://www.mkdocs.org). MkDocs is installed into a venv using `uv sync`.

#### Commands

- `uv run mkdocs serve` - Start the live-reloading docs server.
- `uv run mkdocs build` - Build the documentation site.
- `uv run mkdocs -h` - Print help message and exit.

#### Project layout

```txt
mkdocs.yml    # The configuration file.
docs/
    index.md  # The documentation homepage.
    ...       # Other markdown pages, images and other files.
```
