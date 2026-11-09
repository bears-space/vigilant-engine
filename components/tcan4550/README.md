<!--
# SPDX-AI-Disclosure: ai-generated
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: Port documentation, licensing notes, and validation instructions.
# SPDX-AI-Date: 2026-10-13
-->

# TCAN4550 TI library port for ESP-IDF

This component ports TI's TCAN4550 library to ESP-IDF. The TI core is in `ti/`;
`tcan4550_spi.c` replaces the MSP430 SPI backend. The existing `tcan4550.h`
application API wraps TI's device configuration, timing, message-RAM, transmit,
and receive routines with ESP-IDF error handling and bounded polling.

## Source and license

- Source: [TI SLLC469 revision B](https://www.ti.com/tool/download/SLLC469),
  package version `01.00.00.0B`, released 28 July 2020.
- Archive: `sllc469b.zip`, SHA-256
  `977ff68c68f81655adb1fa250577e7b64ae0da3ef891a9cabef029f4cc08ef96`.
- Imported directory: `TCAN4550_DEMO/tcan4x5x/`; core source version `1.2.2`.
- TI files retain their original copyright and BSD-3-Clause license headers.
  The complete notice is in [ti/LICENSE.txt](ti/LICENSE.txt).
- BSD-3-Clause permits modification and redistribution, including commercial
  use of the TI code. Preserve the notices and disclaimer in source copies and
  reproduce them in documentation/materials accompanying binary distributions.
  TI's name cannot be used to endorse derived products without permission.
- The project-authored adapter, wrapper, and application remain under the
  repository's PolyForm Noncommercial license. Importing TI's library does not
  change that license or make the whole project commercially licensed.
- CMake copies the TI notice to `build/TCAN4550-TI-LICENSE.txt`; include it with
  distributed firmware. See also [the repository notices](../../THIRD_PARTY_NOTICES.md).

Only the TCAN library files were imported; the MSP430 driverlib, demo application,
and generated documentation were excluded. Original CRLF line endings were
normalized to LF and trailing whitespace removed. The upstream file hashes and local changes are recorded in
[ti/UPSTREAM.md](ti/UPSTREAM.md).

## Application setup

Configure the GPIO assignments in `idf.py menuconfig` under
`Component config > TCAN4550 Configuration`. The defaults are:

| Signal | GPIO |
| --- | ---: |
| nCS | 10 |
| SDI/MOSI | 11 |
| SCLK | 12 |
| SDO/MISO | 13 |

The application uses SPI2, SPI mode 0 at 2 MHz, a 40 MHz oscillator, 500 kbit/s
arbitration, and 2 Mbit/s data. Set the oscillator to match the actual hardware.
GPIOs are passed in `tcan4550_config_t`, so the port can be reused on other
ESP-IDF targets. The checked-in `sdkconfig` selects ESP32-C6; select the actual
board's target before flashing. No reset or interrupt GPIO is assumed.

`tcan4550_init()` verifies identity and scratch-register access, initializes
ECC-protected RAM, and starts passive monitoring. The application then starts
normal operation with `tcan4550_start(false)`.
It does not run loopback tests or send example frames. Add application-specific
transmit and receive handling using `tcan4550_transmit()` and `tcan4550_receive()`.

The same menu provides `CONFIG_TCAN4550_DEFAULT_TX_ID` (default `0x123`,
range `0x0`–`0x1FFFFFFF`) for future application transmit frames. CAN IDs belong
to individual frames; there is no controller-wide transmit ID. Initialize
application frames with the configured default as follows:

```c
#include "sdkconfig.h"

tcan4550_frame_t frame = {
    .id = CONFIG_TCAN4550_DEFAULT_TX_ID,
    .extended = CONFIG_TCAN4550_DEFAULT_TX_ID > 0x7FFU,
};
```

This setting does not change receive filters or override explicit frame IDs.

## Direct TI API access

Include `tcan4550_ti.h` to use TI's original functions. Initialize through
`tcan4550_init()` first. Enclose each complete TI operation in a session:

```c
esp_err_t err = tcan4550_ti_begin(100);
if (err == ESP_OK) {
    TCAN4x5x_MCAN_Interrupts interrupts = {0};
    TCAN4x5x_MCAN_ReadInterrupts(&interrupts);
    err = tcan4550_ti_end();
    if (err == ESP_OK) {
        // interrupts.word is now available to the application.
    }
}
```

Sessions serialize the singleton and its configuration cache across tasks. They
must end on the same task, cannot nest, and must not contain calls to the
`tcan4550_*` convenience API. TI's bool/void return types cannot represent
ESP-IDF errors: check both TI's return value (where present) and the session-end
result. A transport error suppresses all remaining transfers in that session,
including FIFO acknowledgements and transmission requests.

Raw TI calls require valid pointers, appropriately sized payload buffers, and
valid configuration arguments. Use TI's configuration functions to update its
cache. Changing the message-RAM layout or operating mode directly invalidates
the convenience wrapper's assumptions; use one interface for those settings.

## Transport and lifecycle

Each burst uses one SPI transaction with hardware-controlled chip select and
big-endian words. The backend supports TI's complete burst range: 1–256 words
(a length byte of zero means 256). Buffers are allocated from internal,
DMA-capable memory. Transfers check device SPI status after the command; status
and device-interrupt accesses remain available for reading/clearing faults.

Initialization failures release allocated resources. `tcan4550_deinit()` enters
standby and releases the SPI device and transport allocations. Initialization
keeps the transceiver in standby until timing, message RAM, and passive monitor
mode are configured. If deinitialization cannot enter standby, it returns the
error and retains the transport so shutdown can be retried.

Initialization and deinitialization require exclusive application ownership: stop all client
tasks first. An already initialized SPI host is borrowed; its pins, DMA support,
and maximum transfer size must be compatible. A borrowed host is never freed.

A failed mode transition or wrapper transport failure blocks subsequent wrapper
TX/RX until recovery. Read diagnostics, clear the hardware cause, then call
`tcan4550_start()` or deinitialize/reinitialize as appropriate. SPI faults can
be cleared using `TCAN4x5x_Device_ClearSPIERR()` in a fresh TI session. Bus-off
recovery requires an explicit restart; the application does not automatically
decide when it is safe to rejoin the bus.

TX timeout requests cancellation and checks pending status for up to another
100 ms. A frame already in progress may complete despite cancellation. Passive
monitor mode rejects wrapper transmission requests.

The FIFO holds 16 frames and uses blocking mode to prevent an element from being
overwritten during separate SPI header/payload reads. The application must drain
the FIFO regularly with `tcan4550_receive()` to avoid losing frames under load.

## Validation

Host regression tests compile the actual wrapper, SPI backend, and TI core
against a register-level mock with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
python components/tcan4550/test/run_host_tests.py
```

If LeakSanitizer is unavailable under a debugger/ptrace sandbox, run with
`ASAN_OPTIONS=detect_leaks=0`; the tests separately check allocation balances.
Tests cover frame/DLC encoding, internal loopback, ECC initialization, 256-word
bursts, timeout cancellation, SPI failure propagation, sessions, timing, RAM
bounds, and failed-init cleanup. Build the firmware with `idf.py build`.
Validation during this port used ESP-IDF 6.0: the full repository built for
ESP32-C6, and an isolated component application built for ESP32-S3 with the
same GPIO assignments. Neither build was flashed.
Physical verification still requires the actual board, a terminated test bus,
and a second CAN-FD node or analyzer.
