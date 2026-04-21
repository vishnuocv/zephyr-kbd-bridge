/*
 * USB Host — public API.
 *
 * Manages the USB host (OTG_HS2) that reads a physical keyboard
 * on the USB-A port and feeds reports into the bridge queue.
 *
 * Also handles USER1 button (PC13) for mode toggle and
 * LED2 (PG10) as bridge mode indicator.
 */

#ifndef USB_HOST_H
#define USB_HOST_H

#include <stdbool.h>
#include <stdint.h>

/* Initialize USB host, VBUS power, button, LED, and start
 * the background USBH process thread. */
int usb_host_start(void);

/* Returns true when bridge is active (reports forwarded to PC). */
bool usb_host_bridge_active(void);

/* Forward keyboard LED state to the physical keyboard.
 * Bit 0=NumLock, 1=CapsLock, 2=ScrollLock. */
void usb_host_set_kbd_leds(uint8_t leds);

#endif
