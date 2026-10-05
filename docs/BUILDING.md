# Build from a clean clone

Requirements: Git, Python 3, CMake, Ninja, an `arm-none-eabi` GCC toolchain,
and Raspberry Pi Pico SDK **2.2.0** with its submodules. Development builds used
Arm GNU 14.2.rel1; the SDK also supports other compatible toolchains.

```sh
git clone --recurse-submodules https://github.com/meech-ward/frank-apple.git
cd frank-apple
git clone --branch 2.2.0 --depth 1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git ../pico-sdk
export PICO_SDK_PATH="$(cd ../pico-sdk && pwd)"
```

Put `arm-none-eabi-gcc` on PATH. If needed, set `PICO_TOOLCHAIN_PATH` to your
Arm toolchain directory. Install `picotool` if the SDK cannot fetch/build it.
These instructions use a POSIX shell (Linux, macOS, or WSL).

```sh
# Choose one board and USB role:
python3 tools/build-public-firmware.py --board pico --usb keyboard
python3 tools/build-public-firmware.py --board tufty --usb console

# Or build all four variants:
python3 tools/build-public-firmware.py
```

The helper only configures/builds; it never flashes a device. Each combination
uses a separate directory so differently configured objects cannot be mixed:

| Hardware | Console | USB keyboard |
| --- | --- | --- |
| Pico 2 W | `build-public-p2w/` | `build-public-p2w-kbd/` |
| Tufty 2350 | `build-public-tufty/` | `build-public-tufty-kbd/` |

Each directory contains `.elf`, `.bin`, and `.uf2` files. Use only the firmware
for your exact hardware. Both USB variants include SSH, hotspot mode, the browser guide and HTTP
client. The console variant exposes USB serial; the keyboard variant uses the
native port as a host. SWD remains available in either mode.

The helper selects 252 MHz, PWM audio, TLS verification, SSH, and local web control;
PS/2 is disabled. Tufty uses the 84 MHz PSRAM setting; Pico uses no PSRAM. The
CMake options remain available for custom builds. Network credentials are read
at startup from `/wifi.ini`; no secrets file is imported at compile time.
After linking, the helper checks the active source/header dependencies and link
map to ensure the retired USB, PS/2, and memory drivers were not pulled in.

## Host tests

Requires a C compiler with AddressSanitizer/UBSan and Node.js (20 or newer).

```sh
python3 tools/check-net-http.py
python3 tools/check-netcard.py
python3 tools/check-wifi-config.py
python3 tools/check-wifi-dhcp.py
python3 tools/check-wifi-access-point.py
python3 tools/check-configure-wifi.py
python3 tools/check-ssh-terminal.py
python3 tools/check-ssh-control.py
python3 tools/check-ssh-identity.py
python3 tests/test_ssh_transport.py --sdk "$PICO_SDK_PATH"
python3 tools/check-video-mixed.py
python3 tools/check-cpu-bus.py
node tools/check-guide.mjs
python3 tools/check-disk-library.py
python3 tools/check-web-upload.py
python3 tools/check-clean-package.py
python3 tools/check-board-memory.py
python3 tools/check-usb-input.py
```

The USB check uses HID definitions from `PICO_SDK_PATH`'s TinyUSB checkout;
it compiles the actual report callbacks and Apple input adapter on the host.
Memory tests cover timing and chip-capacity parsing; they cannot exercise a
physical memory chip. See [EXTERNAL_MEMORY.md](EXTERNAL_MEMORY.md).

The SSH interoperability check also needs Clang/Clang++ and an OpenSSH client.
It compiles the firmware's SSH engine with the SDK's mbedTLS sources, runs
malformed-packet and state tests under sanitizers, and connects to a localhost
test server using ordinary `ssh`. It also exercises the raw TCP adapter with the
SDK's real lwIP packet-buffer implementation. Test-only credentials never enter firmware.
`NETCARD_SSH=OFF` removes the SSH transport for custom builds. Supported SSH
builds reserve 12 KiB of core-0 stack in main SRAM; core 1 retains its 2 KiB video
stack in scratch X. The linker excludes the core-0 stack from the heap.

Optional live checks:

```sh
python3 tools/check-http-live.py
python3 tools/badge-http-get.py --host DEVICE_IP
python3 tools/badge-http-api.py --host DEVICE_IP
python3 tools/check-web-control.py --host DEVICE_IP
```

The first uses the host's TCP/TLS stack and a public echo API. Device checks
require Web control enabled and BASIC ready; they replace the program in RAM.
The GET test writes a disk file only when explicitly given `--save`.
