# Partition Table

The project uses a custom partition layout defined in `partitions.csv`.

## Primary partitions

- `factory`: Recovery firmware (1.1875 MB at `0x10000`)
- `ota_0`: Main firmware (2.75 MB at `0x140000`)
- `otadata`: OTA selection data

The `factory` partition is intended as a stable fallback and should not be overwritten during
normal development.
