/*
 * USB HID Keyboard Bridge — STM32N6570-DK
 *
 * Bridges a USB keyboard (USB-A) to the PC (USB-C).
 * USER1 button toggles bridge on/off. LED2 indicates mode.
 */

#include "kbd_bridge.h"
#include "usb_host.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stm32n6xx_hal.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	int ret;

	/* Step 1: Configure USB2 PHY clocks via LL API.
	 * Must happen before any USB init. HAL_RCCEx_PeriphCLKConfig()
	 * cannot be used — it disrupts USB1's shared HSE clock path.
	 */
	LL_RCC_SetOTGPHYClockSource(LL_RCC_OTGPHY2_CLKSOURCE_HSE_DIV_2);
	LL_RCC_SetOTGPHYCKREFClockSource(
		LL_RCC_OTGPHY2CKREF_CLKSOURCE_HSE_DIV_2_OSC);

	/* Step 2: Initialize USB device side (OTG_HS1 → PC).
	 * Enables VddUSB and shared clock infrastructure.
	 * Does NOT put USB1 on the bus yet.
	 */
	ret = kbd_bridge_init();
	if (ret) {
		LOG_ERR("kbd_bridge_init: %d", ret);
		return ret;
	}

	/* Step 3: Initialize USB host side (OTG_HS2 ← keyboard).
	 * Needs VddUSB from step 2. HAL_HCD_Init's USB_CoreReset
	 * is safe here because USB1 is not on the bus yet.
	 */
	ret = usb_host_start();
	if (ret) {
		LOG_ERR("usb_host_start: %d", ret);
	}

	/* Step 4: Enable USB device on the bus.
	 * PC will enumerate "VicLab N6 USB Keyboard Bridge".
	 */
	ret = kbd_bridge_enable();
	if (ret) {
		LOG_ERR("kbd_bridge_enable: %d", ret);
		return ret;
	}

	LOG_INF("USB keyboard bridge ready");

	/* Step 5: Run the bridge consumer loop (blocks forever). */
	kbd_bridge_run();

	return 0;
}
