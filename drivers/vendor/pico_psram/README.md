# Pico SDK PSRAM subset

These two headers are a narrow BSD-3-Clause adaptation of Raspberry Pi's
`hardware_psram`, downloaded independently from:

- Repository: <https://github.com/raspberrypi/pico-sdk>
- Commit: `5be44290896a620ff77129e71ac0fa7e4661cd05`
- Source: [src/rp2_common/hardware_psram/psram.c](https://github.com/raspberrypi/pico-sdk/blob/5be44290896a620ff77129e71ac0fa7e4661cd05/src/rp2_common/hardware_psram/psram.c)
- Source SHA-256: `2983969dffbfccc0d61e8d36ea3a497c33310d8da8c11071f37875bf943b8442`
- License: [LICENSE](LICENSE), from that commit's `LICENSE.TXT`.

`psram_qmi.h` extracts the CS1 timing/read/write register configuration from
`psram_initialize_internal`, replacing global parameters with an argument.
`psram_params.h` adapts `psram_configure_params` and `psram_eid_to_size` into
host-testable helpers. It uses the upstream defaults of 8,000 ns maximum select
and 18 ns minimum deselect. Changes include wider arithmetic, explicit register
bounds checks, clamping a negative deselect requirement to zero, rejecting zero
refresh protection, and rejecting unknown capacity codes instead of guessing.

The runtime registration, linker sections, flash-devinfo configuration, and
newer flash-command APIs are deliberately excluded. The surrounding
`drivers/board_memory.c` is an original MIT-licensed startup adapter for the
project's SDK 2.2.0. It reads the chip ID through QMI direct mode and then uses
the BSD configuration. It does not contain an allocator or a RAM alias probe.
The old MurmApple/Murmulator PSRAM implementation is not its source.

The adapter's `0xF5` quad-mode exit and `0x9F` serial identification transactions
follow the [AP Memory APS6404L-3SQR datasheet](https://www.apmemory.com/wp-content/uploads/APM_PSRAM_QSPI-APS6404L-3SQR-v2.3-PKG.pdf).
