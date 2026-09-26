# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(BUILD_NAME "frank_apple")

set(BUILD_VERSION "RC24")

# Set board to Pico 2 (RP2350)
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Board configuration variant (M1, M2, P2W)
set(BOARD_VARIANT "M1" CACHE STRING "Board variant: M1, M2, P2W or TUFTY")

# CPU overclock configuration (252, 378, 504 MHz)
set(CPU_SPEED "378" CACHE STRING "CPU clock in MHz: 252, 378, 504")

set(VIDEO_TYPE "VGA" CACHE STRING "Video output: HDMI, VGA, SPI or PAR (8-bit parallel ST7789, Tufty 2350)")

if(VIDEO_TYPE STREQUAL "HDMI")
    set(VIDEO_HDMI ON)
elseif(VIDEO_TYPE STREQUAL "VGA")
    set(VIDEO_VGA ON)
elseif(VIDEO_TYPE STREQUAL "SPI")
    set(VIDEO_SPI ON)
elseif(VIDEO_TYPE STREQUAL "PAR")
    # Same framebuffer contract as the SPI panel driver; only the bus differs.
    set(VIDEO_SPI ON)
    set(VIDEO_PAR ON)
endif()

# Audio output configuration (I2S, PWM or PWME)
set(AUDIO_TYPE "PWM" CACHE STRING "Audio output: I2S, PWM or PWME")

# PSRAM speed configuration (84, 100, 133, 166 MHz target)
# Leave unset for no-PSRAM builds
if(PICO_BOARD STREQUAL "pico2")
    set(PSRAM_SPEED "84" CACHE STRING "PSRAM max frequency in MHz: 84, 100, 133, 166")
endif()

# CPU voltage selection based on speed
if(CPU_SPEED GREATER_EQUAL 504)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_65")
elseif(CPU_SPEED GREATER_EQUAL 378)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_60")
else()
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_50")
endif()

# I2S Audio pin configuration based on board variant
if(BOARD_VARIANT STREQUAL "M1")
    set(I2S_DATA_PIN "26")
    set(I2S_CLOCK_PIN_BASE "27")
elseif(BOARD_VARIANT STREQUAL "P2W")
    set(I2S_DATA_PIN "6")
    set(I2S_CLOCK_PIN_BASE "7")
elseif(BOARD_VARIANT STREQUAL "TUFTY")
    set(I2S_DATA_PIN "16")   # no speaker on the badge; unrouted GPIOs
    set(I2S_CLOCK_PIN_BASE "17")
else()  # M2
    set(I2S_DATA_PIN "9")
    set(I2S_CLOCK_PIN_BASE "10")
endif()

# USB HID Host support (keyboard and gamepad via USB)
# When enabled, USB CDC stdio is disabled!
option(USB_HID_ENABLED "Enable USB HID Host for keyboard/gamepad" ON)

# PS/2 keyboard support toggle
option(PS2_KEYBOARD_ENABLED "Enable PS/2 keyboard input" ON)

# Verbose debug logging toggle
option(DEBUG_LOGS_ENABLED "Enable verbose debug logging" OFF)

option(NETCARD_ENABLED "Virtual WiFi network card in slot 1" OFF)
option(NETCARD_TLS "HTTPS for the network card" OFF)
option(NETCARD_TLS_VERIFY "Verify server TLS certificates with bundled CA roots" ON)
option(NETCARD_WEB_CONTROL "Local browser keyboard and text screen (session opt-in)" OFF)
if(NETCARD_WEB_CONTROL AND NOT NETCARD_ENABLED)
    message(FATAL_ERROR "NETCARD_WEB_CONTROL requires NETCARD_ENABLED")
endif()

message(STATUS "FRANK Apple - Apple IIe Emulator for RP2350")
if (PSRAM_SPEED)
    message(STATUS "Board: ${BOARD_VARIANT}, CPU: ${CPU_SPEED} MHz, PSRAM: ${PSRAM_SPEED} MHz, Voltage: ${CPU_VOLTAGE}")
else ()
    message(STATUS "Board: ${BOARD_VARIANT}, CPU: ${CPU_SPEED} MHz, Voltage: ${CPU_VOLTAGE}")
endif ()
message(STATUS "I2S Audio: DATA=${I2S_DATA_PIN}, CLK_BASE=${I2S_CLOCK_PIN_BASE}")

list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards")
include(pico_sdk_import.cmake)

# Import pico-extras for audio_i2s library
set(PICO_EXTRAS_PATH ${CMAKE_CURRENT_LIST_DIR}/pico-extras CACHE PATH "Path to pico-extras")
if(EXISTS ${PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)
    include(${PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)
endif()

# Keep board/USB/PSRAM configurations isolated; shared archives can silently
# link objects compiled for a different board or feature set.
set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

IF(BOARD_VARIANT STREQUAL "P2W")
    SET(BUILD_NAME "p2w-${BUILD_NAME}")
ELSEIF(BOARD_VARIANT STREQUAL "TUFTY")
    SET(BUILD_NAME "tufty-${BUILD_NAME}")
ELSEIF(BOARD_VARIANT STREQUAL "M2")
    if (PICO_BOARD STREQUAL "pico")
        SET(BUILD_NAME "m2p1-${BUILD_NAME}")
    else()
        SET(BUILD_NAME "m2p2-${BUILD_NAME}")
    endif()
ELSE()
    if (PICO_BOARD STREQUAL "pico")
        SET(BUILD_NAME "m1p1-${BUILD_NAME}")
    else()
        SET(BUILD_NAME "m1p2-${BUILD_NAME}")
    endif()
endif ()

IF(VIDEO_VGA)
    SET(BUILD_NAME "${BUILD_NAME}-VGA")
endif ()
IF(VIDEO_HDMI)
    SET(BUILD_NAME "${BUILD_NAME}-HDMI")
endif ()
IF(VIDEO_PAR)
    SET(BUILD_NAME "${BUILD_NAME}-PAR")
ELSEIF(VIDEO_SPI)
    SET(BUILD_NAME "${BUILD_NAME}-SPI")
endif ()

SET(BUILD_NAME "${BUILD_NAME}-${CPU_SPEED}MHz")

if (PSRAM_SPEED)
    SET(BUILD_NAME "${BUILD_NAME}-P${PSRAM_SPEED}")
endif ()

SET(BUILD_NAME "${BUILD_NAME}-${AUDIO_TYPE}-${BUILD_VERSION}")


project(${BUILD_NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Initialize pico-extras if available
if(COMMAND pico_extras_init)
    pico_extras_init()
endif()

# Copy driver subdirectories from murmdoom structure
add_subdirectory(drivers/ps2kbd)
add_subdirectory(drivers/fatfs)
if(BOARD_VARIANT STREQUAL "TUFTY")
    # No card slot: FatFs drive 0 lives in a region of the 16 MB flash.
    add_subdirectory(drivers/flashdisk)
    set(STORAGE_LIB flashdisk)
else()
    add_subdirectory(drivers/sdcard)
    set(STORAGE_LIB sdcard)
endif()
if(USB_HID_ENABLED)
    add_subdirectory(drivers/usbhid)
endif()

# Collect driver sources based on configuration
set(DRIVER_SOURCES drivers/nespad/nespad.c)

if (PSRAM_SPEED)
    list(APPEND DRIVER_SOURCES
        drivers/psram_init.c
        drivers/psram_allocator.c
    )
endif()

if (VIDEO_HDMI)
    list(APPEND DRIVER_SOURCES drivers/HDMI.c)
endif()

if (VIDEO_VGA)
    list(APPEND DRIVER_SOURCES drivers/vga.c)
endif()

if (VIDEO_PAR)
    list(APPEND DRIVER_SOURCES drivers/par_lcd.c)
elseif (VIDEO_SPI)
    list(APPEND DRIVER_SOURCES drivers/spi_lcd.c)
endif()

# Create a library for drivers
add_library(drivers ${DRIVER_SOURCES})
if (VIDEO_PAR)
    pico_generate_pio_header(drivers ${CMAKE_CURRENT_LIST_DIR}/drivers/par_lcd.pio)
endif()

target_include_directories(drivers PUBLIC drivers drivers/nespad src)
target_compile_definitions(drivers PRIVATE
    BOARD_${BOARD_VARIANT}
)

if (PSRAM_SPEED)
    target_compile_definitions(drivers PRIVATE
        PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED}
    )
endif()

# Apple IIe Emulator sources
add_executable(${BUILD_NAME}
    src/main.c
    src/mii_65c02.c
    src/mii_bank.c
    src/mii_video.c
    src/mii_rom.c
    src/mii.c
    src/mii_rom_iiee.c
    src/mii_rom_iiee_video.c
    src/disk_loader.c
    src/disk_ui.c
    src/mii_startscreen.c
    src/mii_analog.c
    # Disk drive support
    src/mii_slot.c
    src/mii_dd_stub.c
    src/mii_floppy.c
    src/mii_disk2.c
    src/mii_dsk.c
    src/mii_nib.c
    src/mii_woz.c
    src/mii_vcd_stub.c
    src/mii_rom_disk2_p5.c

    src/mii_smartport.c

    # Audio support
    src/mii_audio_i2s.c
    # Note: mii_speaker.c is excluded - stub provided in main.c for RP2350
    src/mii_audio.c
#mockingboard
#    src/mii_mb.c
#    src/mockingboard.c
)

if (PICO_BOARD STREQUAL "pico")
    pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
    target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
    pico_set_boot_stage2(${BUILD_NAME} slower_boot2)
else()
    # RP2350: Use custom linker script
    pico_set_linker_script(${PROJECT_NAME} "${CMAKE_SOURCE_DIR}/memmap.ld")
endif()

target_link_libraries(drivers pico_stdlib hardware_dma hardware_pio hardware_spi hardware_pwm)

target_include_directories(${BUILD_NAME} PRIVATE
    src
    drivers
)

target_compile_definitions(${BUILD_NAME} PRIVATE
    BOARD_${BOARD_VARIANT}
    CPU_CLOCK_MHZ=${CPU_SPEED}
    CPU_VOLTAGE=${CPU_VOLTAGE}
    MII_65C02_DIRECT_ACCESS=1
    MII_RP2350=1
    # I2S Audio configuration
    # NOTE: HDMI uses PIO1, PS/2 uses PIO0 SM0 (claimed dynamically)
    # So I2S uses PIO0 SM2 (claimed statically before PS/2 init)
    PICO_AUDIO_I2S_PIO=0
    PICO_AUDIO_I2S_STATE_MACHINE=2
    PICO_AUDIO_I2S_DMA_IRQ=1
    PICO_AUDIO_I2S_DMA_CHANNEL=10
    PICO_AUDIO_I2S_DATA_PIN=${I2S_DATA_PIN}
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=${I2S_CLOCK_PIN_BASE}
)

if (PSRAM_SPEED)
    target_compile_definitions(${BUILD_NAME} PRIVATE
        PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED}
    )
endif()

# Audio configuration based on AUDIO_TYPE
if(AUDIO_TYPE STREQUAL "I2S")
    target_compile_definitions(${BUILD_NAME} PRIVATE
        # I2S Audio configuration
        # NOTE: HDMI uses PIO1, PS/2 uses PIO0 SM0 (claimed dynamically)
        # So I2S uses PIO0 SM2 (claimed statically before PS/2 init)
        PICO_AUDIO_I2S_PIO=0
        PICO_AUDIO_I2S_STATE_MACHINE=2
        PICO_AUDIO_I2S_DMA_IRQ=1
        PICO_AUDIO_I2S_DMA_CHANNEL=10
        PICO_AUDIO_I2S_DATA_PIN=${I2S_DATA_PIN}
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=${I2S_CLOCK_PIN_BASE}
        FEATURE_AUDIO_I2S=1
    )
elseif(AUDIO_TYPE STREQUAL "PWM")
    target_compile_definitions(${BUILD_NAME} PRIVATE
        FEATURE_AUDIO_PWM=1
    )
elseif(AUDIO_TYPE STREQUAL "PWME")
    target_compile_definitions(${BUILD_NAME} PRIVATE
        FEATURE_AUDIO_PWM_BEEPER=1
    )
endif()

if (VIDEO_HDMI)
    target_compile_definitions(${BUILD_NAME} PRIVATE
        VIDEO_HDMI
        VIDEO=HDMI
    )
endif()

if (VIDEO_VGA)
    target_compile_definitions(${BUILD_NAME} PRIVATE
        VIDEO_VGA
        VIDEO=VGA
    )
endif()

if (VIDEO_SPI)
    target_compile_definitions(${BUILD_NAME} PRIVATE
        VIDEO_SPI
        VIDEO=SPI
    )
endif()
if (VIDEO_PAR)
    target_compile_definitions(${BUILD_NAME} PRIVATE VIDEO_PAR)
    target_compile_definitions(drivers PRIVATE VIDEO_PAR)
endif()

if(PS2_KEYBOARD_ENABLED)
    target_compile_definitions(${BUILD_NAME} PRIVATE ENABLE_PS2_KEYBOARD=1)
else()
    target_compile_definitions(${BUILD_NAME} PRIVATE ENABLE_PS2_KEYBOARD=0)
endif()

if(DEBUG_LOGS_ENABLED)
    target_compile_definitions(${BUILD_NAME} PRIVATE PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=5000)
    target_compile_definitions(drivers PRIVATE ENABLE_DEBUG_LOGS=1)
    target_compile_definitions(${BUILD_NAME} PRIVATE ENABLE_DEBUG_LOGS=1)
else()
    target_compile_definitions(drivers PRIVATE ENABLE_DEBUG_LOGS=0)
    target_compile_definitions(${BUILD_NAME} PRIVATE ENABLE_DEBUG_LOGS=0)
endif()

# Optimization for maximum performance on RP2350
# -O3: Maximum optimization including loop vectorization
# -ffunction-sections -fdata-sections: Allow linker to remove unused code
# -flto: Link-time optimization for cross-file inlining
# -fno-strict-aliasing: Allow type punning (common in emulators)
# -march=armv8-m.main+fp+dsp: Target Cortex-M33 architecture
target_compile_options(${BUILD_NAME} PRIVATE 
    -O3
    -ffunction-sections 
    -fdata-sections
    -fno-strict-aliasing
)

target_link_libraries(${BUILD_NAME}
    pico_stdlib
    pico_multicore
    pico_audio_i2s
    hardware_dma
    hardware_pio
    hardware_clocks
    hardware_vreg
    hardware_spi
    drivers
    ps2kbd
    ${STORAGE_LIB}
    fatfs
)

if(USB_HID_ENABLED)
    target_link_libraries(${BUILD_NAME} usbhid)
endif()

# USB CDC stdio: enabled for debugging, but disabled when USB HID is enabled
# (USB port can only be Host OR Device, not both)
if(USB_HID_ENABLED)
    pico_enable_stdio_usb(${BUILD_NAME} 0)
    message(STATUS "USB CDC stdio disabled (USB HID Host is enabled)")
else()
    pico_enable_stdio_usb(${BUILD_NAME} 1)
endif()
if(BOARD_VARIANT STREQUAL "TUFTY")
    pico_enable_stdio_uart(${BUILD_NAME} 0)   # no UART pins on the badge
else()
    pico_enable_stdio_uart(${BUILD_NAME} 1)
endif()

if(NETCARD_ENABLED)
    target_sources(${BUILD_NAME} PRIVATE src/mii_netcard.c src/net_http.c src/wifi_config.c src/wifi_config_file.c)
    if(NETCARD_WEB_CONTROL)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/src/web_control.html")
        file(READ "${CMAKE_CURRENT_LIST_DIR}/src/web_control.html" _web_hex HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _web_bytes "${_web_hex}")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
        file(WRITE "${CMAKE_BINARY_DIR}/generated/web_page.h" "static const unsigned char web_page[] = {${_web_bytes}};\n")
        target_sources(${BUILD_NAME} PRIVATE src/web_control.c src/disk_library.c)
        target_compile_definitions(${BUILD_NAME} PRIVATE NETCARD_WEB_CONTROL=1)
    endif()
    target_link_libraries(${BUILD_NAME}
        pico_cyw43_arch_lwip_poll
    )
    if(NETCARD_TLS)
        target_link_libraries(${BUILD_NAME}
            pico_lwip_mbedtls
            pico_mbedtls
        )
        target_compile_definitions(${BUILD_NAME} PRIVATE
            NETCARD_TLS=1
        )
        if(NETCARD_TLS_VERIFY)
            # Bundle the CA roots as a NUL-terminated C array for mbedTLS.
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_LIST_DIR}/certs/supabase-roots.pem"
                "${CMAKE_CURRENT_LIST_DIR}/certs/amazon-roots.pem")
            file(READ "${CMAKE_CURRENT_LIST_DIR}/certs/supabase-roots.pem" _nc_ca_hex HEX)
            file(READ "${CMAKE_CURRENT_LIST_DIR}/certs/amazon-roots.pem" _nc_amazon_hex HEX)
            string(APPEND _nc_ca_hex "${_nc_amazon_hex}")
            string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _nc_ca_bytes "${_nc_ca_hex}")
            file(WRITE "${CMAKE_BINARY_DIR}/generated/netcard_ca.h"
                "#pragma once\nstatic const unsigned char netcard_ca_pem[] = { ${_nc_ca_bytes}0x00 };\n#define NETCARD_CA_PEM_LEN sizeof(netcard_ca_pem)\n")
            target_compile_definitions(${BUILD_NAME} PRIVATE
                NETCARD_TLS_VERIFY=1
                ALTCP_MBEDTLS_AUTHMODE=MBEDTLS_SSL_VERIFY_REQUIRED
            )
        endif()
    endif()
    target_compile_definitions(${BUILD_NAME} PRIVATE
        NETCARD_ENABLED=1
        CYW43_PIO_CLOCK_DIV_INT=4
    )
    # Network settings are read from /wifi.ini; no project or Wi-Fi credentials
    # are compiled in, even if an old build cache still names SECRETS_FILE.
    target_include_directories(${BUILD_NAME} PRIVATE ${CMAKE_BINARY_DIR}/generated)
endif()

# Create UF2 output
pico_add_extra_outputs(${BUILD_NAME})

target_link_options(${BUILD_NAME} PRIVATE -Xlinker --print-memory-usage --data-sections)
