# Troubleshooting

## `IDF_PATH is not set`

You are not in an ESP-IDF exported shell.

- Windows: open ESP-IDF PowerShell
- Linux/macOS: source `export.sh`

## `BIN not found`

Make sure your build succeeded and the binaries exist:

- `build/vigilant-engine.bin`
- `vigilant-engine-recovery/build/vigilant-engine-recovery.bin`

## Port or permission errors

- Close serial monitors (for example `idf.py monitor`) before flashing
- Verify the serial port is correct

## Always booting into the wrong partition

Use `otatool.py` to select the correct slot:

```sh
python $IDF_PATH/components/app_update/otatool.py --port /dev/ttyACM0 switch_ota_partition --slot 0
```

## Flash size mismatch

`flash.py` runs `idf.py reconfigure`, which rebuilds `sdkconfig` from `sdkconfig.defaults`. Set the
correct flash size in `sdkconfig.defaults` so rebuilds keep the proper values.
