# External memory

`drivers/board_memory.h` provides startup initialization, detected capacity, and
the CS1 memory window at `0x11000000`. Existing disk buffers and the HDD cache
use that window directly. There is no external-memory allocator.

Initialization must run after setting the system clock and before launching
core 1 or DMA that accesses flash/PSRAM. Interrupts are disabled during bus
configuration; all instructions executed while QMI direct mode is enabled are
placed in SRAM. Identification does not write to PSRAM. Known APS6404/ISSI
density IDs report 2, 4, 8, or 16 MiB; absent/unsupported devices fail startup.
At Tufty's 252 MHz system clock and 84 MHz PSRAM target the timing fields are
CLKDIV=3, RXDELAY=3, MAX_SELECT=31, MIN_DESELECT=3.

SDK 2.2.0 remains unchanged. CS1 is left outside the ROM's flash-devinfo control,
so its `hardware_flash` implementation saves/restores the custom CS1 timing,
read command, and read format around flash operations. Flashdisk uses those
SDK flash APIs under its existing interrupt/multicore safety handling; it does
not implement a separate CS1 register save/restore. Both the SDK preservation
and the project's flash-safe execution must remain in place.
Initialization rejects a ROM-owned CS1 configuration instead of silently
leaving flash writes able to invalidate quad mode.

RP2350-E14 concerns the A2 bootrom configuring GPIO 0's pads instead of the
configured CS1 pin; it is not a PSRAM clock-frequency erratum. This adapter
explicitly configures the selected GPIO through SDK 2.2.0's
`gpio_set_function`, which removes pad isolation on that pin, and leaves CS1
outside ROM flash-devinfo control. No additional E14 timing workaround is
needed for the 252 MHz / 84 MHz configuration. Retain that explicit GPIO
configuration if changing startup or SDK integration. See the
[RP2350 datasheet, RP2350-E14](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf#page=1359)
and [SDK 2.2.0 GPIO implementation](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/hardware_gpio/gpio.c#L38).

The configuration subset has its own [BSD license and provenance](../drivers/vendor/pico_psram/README.md).
The adapter and its host tests use the project's MIT downstream license.
Binary/release packages must include the vendor BSD copyright, conditions,
and disclaimer in their license materials, alongside the downstream license.

Run the meaningful host checks from the repository root:

```sh
cc -std=c11 -Wall -Wextra -Werror -I. tests/board_memory_params_test.c -o /tmp/board-memory-test
/tmp/board-memory-test
```

They cover known board timings, the 100 MHz receive-delay boundary, more than
2,000 clock/target combinations, timing-field overflow and zero protection,
low-clock deselect underflow, and valid/unknown chip capacity IDs. Firmware
builds verify SDK compatibility. Neither a host test nor a firmware build can
validate physical signal timing or flash-write behavior on a board.

Hardware acceptance still requires cold and warm boot, disk-buffer/cache
read/write checks, and a flashdisk write followed by PSRAM data verification
on the intended board. No hardware test or flashing is implied by these checks.
