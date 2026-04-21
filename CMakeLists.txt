cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(usb_kbd_bridge)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

target_sources(app PRIVATE
	src/main.c
	src/kbd_bridge.c
	src/usb_host.c
	src/usbh_conf.c
)

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

target_sources(app PRIVATE
	${ZEPHYR_HAL_STM32_MODULE_DIR}/stm32cube/stm32n6xx/drivers/src/stm32n6xx_hal_hcd.c
)
