<!--
# SPDX-AI-Disclosure: ai-generated
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: Upstream provenance, checksums, and modification documentation.
# SPDX-AI-Date: 2026-10-13
-->

# TI library provenance and modifications

Source: https://www.ti.com/tool/download/SLLC469

Archive URL:
https://dr-download.ti.com/software-development/driver-or-library/MD-zllkJ5TWxx/01.00.00.0B/sllc469b.zip

Archive SHA-256:
`977ff68c68f81655adb1fa250577e7b64ae0da3ef891a9cabef029f4cc08ef96`

Original archive-entry SHA-256 values (before CRLF normalization or edits):

| File in `TCAN4550_DEMO/tcan4x5x/` | SHA-256 |
| --- | --- |
| TCAN4550.c | d2105e864070adcd9362ed2ab7c756219571603f6e4734d568f65284f99d91fd |
| TCAN4550.h | 000a74cb560155fa0ae041a5169421eb49fadfb431cb2df85fcea07614aa2cd5 |
| TCAN4x5x_Data_Structs.h | 37e8d7ba7e8899185ed4e42c407362ba0ea63ec55c363d750ee77fc5c3548f08 |
| TCAN4x5x_Reg.h | 65f39ea8d49d1f7c5e72c475c43cf1c27c4202fd970d0f3efd65af4054bb6d3a |
| TCAN4x5x_SPI.h | d241837f2e9e9561378886c6f82b78760a590a4605e1a6b58ecffee32a372c83 |

All imported files retain TI's copyright and license. Line endings were normalized
and trailing whitespace removed. Local modifications:

- `TCAN4x5x_SPI.h`: replace MSP430 driverlib and hardware macros with standard
  C headers; keep the AHB function signatures for the ESP-IDF backend.
- `TCAN4550.h`: add C++ linkage guards and the missing ReadXIDFilter declaration.
- `TCAN4550.c`: initialize zero-length RX return values; interpret Classical
  DLC 9–15 as eight bytes and RTR as no payload in FIFO and dedicated-buffer reads.
- Validate total message-RAM usage against 2048 bytes before changing registers
  or caches, and correct the final bounds check. Use blocking RX FIFO 0 to keep
  header and payload reads consistent while frames arrive.
- Validate TX buffer count and payload capacity, initialize at least eight
  payload bytes for ECC (including empty and remote frames), and use unsigned
  shifts for buffer bitmaps.
- Fix Device_SetMode readback comparison to compare only the mode bits.
- Scale simple timing TDCO by the data prescaler; enable TDC only with actual
  prescalers 1 or 2. Reject unsupported TDC settings in the raw timing API.

`TCAN4x5x_Data_Structs.h` and `TCAN4x5x_Reg.h` have only whitespace changes.
The MSP430 `TCAN4x5x_SPI.c` was replaced rather than imported.
