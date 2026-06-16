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
