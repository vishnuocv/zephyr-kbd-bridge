/*
 * USB HID Keyboard Bridge — device side API.
 *
 * Manages the USB device (OTG_HS1) that appears as a HID keyboard
 * to the PC, and provides the message queue for report forwarding.
 */

#ifndef KBD_BRIDGE_H
#define KBD_BRIDGE_H

#include <stdint.h>

/* Boot-protocol HID keyboard report size (8 bytes). */
#define KB_REPORT_SIZE 8

/* Initialize USB device stack and register HID keyboard.
 * Does NOT enable the device on the bus. */
int kbd_bridge_init(void);

/* Enable USB device on the bus. Call after usb_host_start(). */
int kbd_bridge_enable(void);

/* Consumer loop — reads reports from queue and sends to PC.
 * Blocks forever. Call as the last step in main(). */
void kbd_bridge_run(void);

/* Enqueue an 8-byte HID report for delivery to the PC.
 * Non-blocking. Returns 0 on success, -ENOMSG if queue full. */
int kbd_bridge_enqueue(const uint8_t report[KB_REPORT_SIZE]);

#endif
