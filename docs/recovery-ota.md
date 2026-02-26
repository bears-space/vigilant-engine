# Recovery & OTA

Vigilant Engine uses two primary app partitions:

- `factory`: Recovery firmware (stable fallback image, very limited size)
- `ota_0`: Main firmware (day-to-day runtime)

The recovery firmware is intended as a safe fallback and a way to perform OTA updates even if the
main image is corrupted.

## OTA workflow overview

1. Device boots into `ota_0` under normal conditions.
2. If an OTA update fails or the main firmware is corrupted, the device can fall back to `factory`.
3. The recovery firmware provides a path to re-flash the main image safely.

## Switching partitions manually

You can manually switch OTA slots using ESP-IDF tooling when needed:

```sh
python $IDF_PATH/components/app_update/otatool.py --port /dev/ttyACM0 switch_ota_partition --slot 0
```

Adjust the slot number and serial port to match your device.
