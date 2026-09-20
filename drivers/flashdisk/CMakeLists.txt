# flashdisk: FatFs physical drive 0 backed by a region of the on-board QSPI flash.
# Replaces drivers/sdcard on boards without a card slot (Tufty 2350).
if (NOT TARGET flashdisk)
    add_library(flashdisk INTERFACE)
    target_sources(flashdisk INTERFACE ${CMAKE_CURRENT_LIST_DIR}/flashdisk.c)
    target_link_libraries(flashdisk INTERFACE fatfs pico_stdlib hardware_flash pico_flash pico_multicore hardware_sync)
    target_include_directories(flashdisk INTERFACE ${CMAKE_CURRENT_LIST_DIR})
endif ()
