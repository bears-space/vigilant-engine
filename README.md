# Vigilant Engine

Vigilant Engine is the framework on which all ESP-based nodes of the **Aerobear** project are built.  
It provides the fundamental building blocks for connected, remotely maintainable devices, including
reliable OTA updates, flexible network modes, and peripheral support.

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

## Partition setup and the Flasher

Vigilant engine is made from two main partitions, the factory partition, and the ota_0 partition.
- **`factory` (Factory / Recovery, 1 MB @ 0x10000):** This is the fallback firmware. It’s meant to be a stable recovery image you can always boot into if the main app gets corrupted or an OTA update fails. However, it is mainly used to flash a new firmware via **OTA**. *More information is provided in the "Flashing the ESP32" section*

- **`ota_0` (Main app slot, ~2.94 MB @ 0x110000):** This is the primary firmware slot the device normally runs. It’s much larger than `factory`, so it can hold the full-featured application build and is the one we’ll typically update/replace during development.

## Flash Helper (`flash.py`) — Vigilant Engine

This repository uses a custom partition layout:

- **`factory`** → Recovery firmware (1 MB)
- **`ota_0`** → Main firmware (rest of flash)
- **`otadata`** → OTA boot selection data

Because a `factory` app partition exists, running **`idf.py flash`** will typically flash the application into **`factory`** (and overwrite Recovery).  
This script avoids that by **flashing by partition name**.

---

## What `flash.py` does

`flash.py` is a cross-platform (Windows/Linux\*/macOS\*) helper that:

1. **Builds** the requested firmware (optional)
2. **Flashes** the produced `.bin` into the correct partition using ESP-IDF’s `parttool.py`
3. Prevents accidental overwrites of the Recovery firmware

It supports two targets:

- **Main firmware** → flashes `build/vigilant-engine.bin` into partition **`ota_0`**
- **Recovery firmware** → flashes `recovery/build/vigilant-engine-recovery.bin` into partition **`factory`**

*\*Note that macOS and Linux are untested at this point of time.*

---

## Requirements

- **ESP-IDF 5.5.1** installed
- You must run this from an environment where ESP-IDF is exported:
  - Windows: *ESP-IDF PowerShell* / *ESP-IDF Command Prompt*
  - Linux/macOS: run `export.sh` in your shell
  - **Ideally use the built in ESP-IDF terminal in vs-code**

The script relies on:

- `IDF_PATH` environment variable (set by ESP-IDF)
- A working Python installation

---

## Usage

From the repository root:

### Flash Main firmware (to `ota_0`)

**Windows**

```powershell
python .\flash.py main --port COM7
```

**Linux/macOS**

```sh
python3 ./flash.py main --port /dev/ttyACM0
```

### Flash Recovery firmware (to `factory`)

**Windows**

```powershell
python .\flash.py recovery --port COM7
```

**Linux/macOS**

```sh
python3 ./flash.py recovery --port /dev/ttyACM0
```

## Options

---
**`--port`** **(required)** Serial port of the ESP device.

Examples:

- Windows: `COM3`, `COM7`
- Linux: `/dev/ttyACM0`, `/dev/ttyUSB0`

---

**`--baud`** *(optional)* Baud rate for flashing. Default is `921600`.

Example:

- Windows: `python3 ./flash.py main --port COM7 --baud 460800`
- Linux: `python3 ./flash.py main --port /dev/ttyACM0 --baud 460800`

---

**`--no-build`** *(optional)* Skips the build step and only flashes the existing `.bin`.

Example:

- Windows: `python3 ./flash.py main --port COM7 --no-build`
- Linux: `python3 ./flash.py main --port /dev/ttyACM0 --no-build`

---

## Troubleshooting

`IDF_PATH is not set` - You are not in an ESP-IDF exported shell.

- Windows: open ESP-IDF PowerShell
- Linux/macOS: source export.sh

`BIN not found` - Make sure the build succeeded and the files exist:

- `build/vigilant-engine.bin`
- `recovery/build/vigilant-engine-recovery.bin`

#### Permission / port errors

- Close serial monitors (e.g. idf.py monitor) before flashing
- Ensure the correct port is used

#### Safety notes

- Use main for everyday development — it will not touch Recovery.
- Use recovery only when you intentionally want to update the Recovery partition.
- Keep your partition table stable across devices if you flash existing hardware in the field.
