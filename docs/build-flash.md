# Build & Flash

Vigilant Engine uses a two-partition layout with a recovery firmware in `factory` and the main
firmware in `ota_0`. To avoid accidentally overwriting recovery, use `flash.py` instead of
`idf.py flash`.

## Build main firmware

```sh
idf.py build
```

## Build recovery firmware

```sh
cd vigilant-engine-recovery
idf.py build
```

## Flash main firmware (recommended)

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

## Common options

- `--port` (required): Serial port for the device
- `--baud` (optional): Default is `921600`
- `--no-build` (optional): Skip build and only flash binaries. Useful when you use `idf.py build` and just want to flash
