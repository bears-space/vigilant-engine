# Build & Flash

Vigilant Engine uses a two-partition layout with a recovery firmware in `factory` and the main
firmware in `ota_0`. For flashing individual partitions, use the `flash.py` helper script, otherwise just use the built-in `idf.py` flasher, to flash both partitions.

## Build firmware
The following command builds both partitions:
```sh
idf.py build
```

## Flashing
To flash, use the following command, or press the `flash` button in vs-code:
```sh
idf.py flash
```
If you want to flash just individual partitions, use the following commands:

## Flash main firmware

Windows:

```powershell
python .\flash.py main --port COM7
```

Linux/macOS:

```sh
python3 ./flash.py main --port /dev/ttyACM0
```

## Flash recovery firmware

Windows:

```powershell
python .\flash.py recovery --port COM7
```

Linux/macOS:

```sh
python3 ./flash.py recovery --port /dev/ttyACM0
```

## Common options for `flash.py`

- `--port` (required): Serial port for the device
- `--baud` (optional): Default is `921600`
- `--no-build` (optional): Skip build and only flash binaries. Useful when you use `idf.py build` and just want to flash
