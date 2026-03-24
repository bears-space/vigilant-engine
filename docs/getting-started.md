# Getting Started

This project is built on ESP-IDF (currently 5.5.2). Make sure your ESP-IDF
environment is exported before building or flashing.

## Prerequisites

- ESP-IDF installed and exported in your shell
- Python available in your ESP-IDF environment
- ^ To avoid this just use esp-idf in vs-code
- NodeJS & NPM installed
- A compatible ESP target and a USB serial connection

## Environment setup

Use the ESP-IDF terminal in VS Code or run the ESP-IDF export script in your shell. The tooling
relies on `IDF_PATH` and standard ESP-IDF environment variables.

## First build (main firmware)

From the repository root:

```sh
idf.py build
```

If you need to set a specific target or configuration, do so before building:

```sh
idf.py set-target <target>
idf.py menuconfig
```

## First build (recovery firmware)

The recovery firmware lives in `vigilant-engine-recovery`. Build it from that directory:

```sh
cd vigilant-engine-recovery
idf.py build
```
