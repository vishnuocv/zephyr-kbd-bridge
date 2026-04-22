# USB HID Keyboard Bridge — STM32N6570-DK

Bridges a physical USB keyboard (USB-A port) to a PC (USB-C port) through
the STM32N6, with a hardware button to toggle the bridge on/off.

```
USB Keyboard ──► USB-A (OTG_HS2, host) ──► STM32N6 ──► USB-C (OTG_HS1, device) ──► PC
```

## Prerequisites

- Zephyr RTOS v4.3+
- STM32N6570-DK board
- ST USB Host Middleware cloned to `~/zephyr/stm32-mw-usb-host/`

```bash
cd ~/zephyr
git clone https://github.com/STMicroelectronics/stm32-mw-usb-host
```

## Build and Flash

```bash
west build -b stm32n6570_dk zephyr-kbd-bridge -p always
west flash
```

## Hardware Connections

| Port     | Function                        |
|----------|---------------------------------|
| USB-C    | Connects to PC (device mode)    |
| USB-A    | Connects keyboard (host mode)   |
| USER1    | Toggle bridge ON/OFF            |
| LED2     | Red = bridge active             |

## Project Structure

```
src/
├── main.c          Initialization sequence (5 API calls)
├── kbd_bridge.h    Device-side API
├── kbd_bridge.c    USB device (HID keyboard on USB-C)
├── usb_host.h      Host-side API
├── usb_host.c      USB host (keyboard on USB-A) + button + LED
├── usbh_conf.h     ST middleware configuration
└── usbh_conf.c     HAL HCD callbacks + USBH_LL bridge
```

## API Reference

### kbd_bridge.h

`int kbd_bridge_init(void)` — Initialize the USB device stack and register
the HID keyboard descriptor. Does not enable the device on the bus. Call
before `usb_host_start()`.

`int kbd_bridge_enable(void)` — Enable the USB device on the bus. The PC
will enumerate the HID keyboard after this call. Call after
`usb_host_start()`.

`void kbd_bridge_run(void)` — Consumer loop that reads HID reports from
the message queue and submits them to the PC. Blocks forever. Call as
the last step in `main()`.

`int kbd_bridge_enqueue(const uint8_t report[8])` — Enqueue a boot-protocol
HID report (8 bytes) for delivery to the PC. Called by `usb_host.c` when
the physical keyboard sends a report. Returns 0 on success, -ENOMSG if
the queue is full.

### usb_host.h

`int usb_host_start(void)` — Initialize the USB host stack, enable VBUS
power to the USB-A port, and start the background thread that polls the
keyboard. Also initializes the USER1 button and LED2.

`bool usb_host_bridge_active(void)` — Returns true when the bridge is
active (reports are forwarded to the PC).

`void usb_host_set_kbd_leds(uint8_t leds)` — Forward LED state to the
physical keyboard. Bit 0 = NumLock, bit 1 = CapsLock, bit 2 = ScrollLock.
Called automatically when the PC sends LED updates.

## Init Order

The STM32N6 has two USB OTG controllers that share PHY clock infrastructure.
The initialization order is critical:

```
1. LL_RCC_SetOTGPHYClockSource()        USB2 PHY clock (LL API, not HAL)
   LL_RCC_SetOTGPHYCKREFClockSource()   USB2 ref clock

2. kbd_bridge_init()                     USB1 software init (enables VddUSB)

3. usb_host_start()                      USB2 init (HAL_HCD_Init + CoreReset)

4. kbd_bridge_enable()                   USB1 goes live on the bus

5. kbd_bridge_run()                      Consumer loop (blocks)
```

Step 1 uses LL API register writes instead of `HAL_RCCEx_PeriphCLKConfig()`
which disrupts USB1's shared HSE clock path.

Step 3 must happen after step 2 (needs VddUSB) but before step 4
(`USB_CoreReset` on USB2 would disrupt USB1 if it were on the bus).

## Integration Guide

To add the keyboard bridge to an existing Zephyr project:

1. Copy `src/kbd_bridge.*`, `src/usb_host.*`, `src/usbh_conf.*` into
   your project.

2. Add to your `CMakeLists.txt`:

```cmake
# Application sources
target_sources(app PRIVATE
    src/kbd_bridge.c
    src/usb_host.c
    src/usbh_conf.c
)

# ST USB Host Middleware
set(USBH_MW $ENV{HOME}/zephyr/stm32-mw-usb-host)
target_sources(app PRIVATE
    ${USBH_MW}/Core/Src/usbh_core.c
    ${USBH_MW}/Core/Src/usbh_ctlreq.c
    ${USBH_MW}/Core/Src/usbh_ioreq.c
    ${USBH_MW}/Core/Src/usbh_pipes.c
    ${USBH_MW}/Class/HID/Src/usbh_hid.c
    ${USBH_MW}/Class/HID/Src/usbh_hid_keybd.c
    ${USBH_MW}/Class/HID/Src/usbh_hid_mouse.c
    ${USBH_MW}/Class/HID/Src/usbh_hid_parser.c
)
target_include_directories(app PRIVATE
    src
    ${USBH_MW}/Core/Inc
    ${USBH_MW}/Class/HID/Inc
)

# HAL HCD driver
target_sources(app PRIVATE
    ${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube/stm32n6xx/drivers/src/stm32n6xx_hal_hcd.c
)
```

3. Add to your `prj.conf`:

```
CONFIG_USB_DEVICE_STACK_NEXT=y
CONFIG_USBD_HID_SUPPORT=y
CONFIG_HWINFO=y
CONFIG_HEAP_MEM_POOL_SIZE=8192
```

4. Add to your `app.overlay`:

```dts
&zephyr_udc0 {
    status = "okay";
};

/ {
    hid_dev_0: hid_dev_0 {
        compatible = "zephyr,hid-device";
        label = "HID0";
        protocol-code = "keyboard";
        in-report-size = <64>;
        in-polling-period-us = <1000>;
    };
};
```

5. Call the 5 functions from your `main()` in order.

## Board-Specific Notes (STM32N6570-DK)

| Signal        | GPIO  | Function                              |
|---------------|-------|---------------------------------------|
| PWR_USB2_EN   | PB9   | VBUS switch enable (active HIGH)      |
| USER1 button  | PC13  | Mode toggle (active HIGH, pull-down)  |
| LED2 red      | PG10  | Bridge mode indicator (active HIGH)   |
| USB2_OTG_HS   | —     | IRQ 178, base 0x58060000              |
