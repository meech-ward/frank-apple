// SPDX-License-Identifier: MIT
#pragma once

// Call first in main, before PSRAM, core 1, USB, display, or storage starts.
// A short RESET press boots normally; holding RESET for two seconds powers off.
// RESET has already reset the chip, so this cannot save the previous session.
void tufty_power_boot_check(void);
