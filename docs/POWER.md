# Tufty power button

With this firmware, hold **RESET for about two seconds** to turn the Tufty off.
The four rear LEDs fill during the hold and go out at shutdown. Release RESET.
A short RESET press turns it on again; a short press while running restarts it.
HOME + RESET still enters the RP2350 bootloader for firmware updates.

Save your work first and wait for disk activity to finish. From DOS BASIC, use
`SAVE name`, then `CATALOG`, and wait for the listing and disk activity to finish
before pressing RESET. RESET immediately resets the chip: the long-press code
runs during the next startup and cannot save RAM or flush the previous session.
This is a fresh boot on wake, not a suspended Apple II session.

Off uses the RP2350's shipping-style low-power state. The display, radio,
peripheral rail and processor are shut down. It is not a physical battery
disconnect; standby circuitry remains powered, and a USB-powered keyboard or
adapter may remain powered from its external supply. RESET wakes the badge;
the front buttons and RTC do not. Battery current has not been measured.

## Implementation and provenance

`drivers/tufty_power.c` adapts the GPIO, USB PHY and RP2350 POWMAN sequence from
[Pimoroni badgeware-cpp](https://github.com/pimoroni/badgeware-cpp/blob/7d1ef0882c6e9dcf374542528f6f211e287b5afb/lib/powman.c),
pinned to commit `7d1ef0882c6e9dcf374542528f6f211e287b5afb` under the
[Pimoroni MIT license](../licenses/Pimoroni-Tufty-MIT.txt).

This adaptation is Tufty-only and runs first in `main`, before PSRAM, core 1,
USB, Wi-Fi, the display, storage or the emulator starts. It intentionally omits
RTC access, double-tap handling, timed wake and front-button wake. A two-second
hold with simple LED progress replaces the PWM animation. Prior wake sources
are disabled; power-transition failures reboot instead of hanging with a dark
screen. The function is not a runtime shutdown API.

The host check (`python3 tools/check-tufty-power.py`) executes the production
driver with simulated pins and power hardware. It covers cold/watchdog boots,
early release, full hold, timer wrap, LED feedback, GPIO/USB parking, wake state
and rejected power transitions. Both Tufty and both Pico build profiles pass;
only Tufty links this driver. Host checks do not prove physical shutdown/wake
or battery current. See [validation status](VALIDATION.md).
