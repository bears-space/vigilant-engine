# Flash Helper (`flash.py`)

This repository uses `flash.py` to avoid overwriting the recovery firmware when flashing.

## What it does

- Builds firmware (optional)
- Flashes the correct image into the correct partition using `parttool.py`
- Prevents accidental overwrite of the recovery image

## Targets

- Main firmware: `build/vigilant-engine.bin` -> `ota_0`
- Recovery firmware: `vigilant-engine-recovery/build/vigilant-engine-recovery.bin` -> `factory`

## Usage

Windows:

```powershell
python .\flash.py main --port COM7
python .\flash.py recovery --port COM7
```

Linux/macOS:

```sh
python3 ./flash.py main --port /dev/ttyACM0
python3 ./flash.py recovery --port /dev/ttyACM0
```

## Options

- `--port` (required): Serial port of the ESP device
- `--baud` (optional): Baud rate, default `921600`
- `--no-build` (optional): Skip build step and flash existing binaries
