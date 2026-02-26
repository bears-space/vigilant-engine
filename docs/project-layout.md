# Project Layout

This repository contains the main firmware, recovery firmware, shared components, and a frontend
subproject. The key paths are:

- `main/`: Main firmware application source
- `components/`: Reusable components shared across firmware targets
- `managed_components/`: ESP-IDF managed components
- `vigilant-engine-recovery/`: Recovery firmware project
- `vigilant-engine-frontend/`: Frontend project for VE and the Recovery app
- `docs/`: MkDocs content
- `sdkconfig` and `sdkconfig.defaults`: Build configuration and defaults
- `partitions.csv`: Partition table for firmware layout
- `flash.py`: Safe flashing helper for main and recovery images

## Where to add new functionality

- Project-specific **(NOT VE)** features and application logic: `main/`
- Features you need in vigilant-engine: `components/vigilant_engine`
- Hardware drivers and reusable logic: `components/`
- Recovery flow or minimal boot image changes: `vigilant-engine-recovery/`