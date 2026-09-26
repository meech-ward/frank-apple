/* SPDX-License-Identifier: MIT
 * Host shim: retain actual TinyUSB HID definitions; stub only host transport.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "class/hid/hid.h"
#define BOARD_TUH_RHPORT 0
#ifndef CFG_TUH_HID
#define CFG_TUH_HID 8
#endif
typedef struct {
    uint8_t report_id;
    uint16_t usage_page;
    uint8_t usage;
} tuh_hid_report_info_t;
uint8_t tuh_hid_interface_protocol(uint8_t device, uint8_t instance);
bool tuh_hid_receive_report(uint8_t device, uint8_t instance);
uint8_t tuh_hid_parse_report_descriptor(tuh_hid_report_info_t *info,
    uint8_t count, const uint8_t *report, uint16_t len);
bool tuh_init(uint8_t rhport);
void tuh_task(void);
void tuh_hid_mount_cb(uint8_t, uint8_t, const uint8_t *, uint16_t);
void tuh_hid_umount_cb(uint8_t, uint8_t);
void tuh_hid_report_received_cb(uint8_t, uint8_t, const uint8_t *, uint16_t);
