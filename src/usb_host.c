/*
 * USB Host — ST USB Host Middleware on OTG_HS2.
 *
 * Runs the USBH_Process() state machine in a background thread,
 * forwards HID keyboard reports to the bridge queue, handles
 * USER1 button mode toggle, and forwards LED state.
 *
 * Hardware (STM32N6570-DK):
 *   USER1 button: PC13, active HIGH, pull-down
 *   LED2 (red):   PG10, active HIGH
 *   VBUS switch:  PB9 → STMPS2151 (controlled by usbh_conf.c)
 */

#include "usb_host.h"
#include "kbd_bridge.h"
#include "usbh_conf.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "usbh_core.h"
#include "usbh_hid.h"
#include "usbh_hid_keybd.h"

LOG_MODULE_REGISTER(usb_host, LOG_LEVEL_INF);

static USBH_HandleTypeDef hUSBHost;
static volatile bool bridge_active = true;
static volatile uint8_t pending_leds;
static volatile bool leds_dirty;

/* ---- GPIO: button (PC13) and LED (PG10) ---- */

#define BTN_PORT   GPIOC
#define BTN_PIN    13U
#define LED_PORT   GPIOG
#define LED_PIN    10U

/* Configure button as input with pull-down, LED as push-pull output. */
static void gpio_init(void)
{
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();

	/* PC13: input (00), pull-down (10). */
	BTN_PORT->MODER &= ~(3U << (BTN_PIN * 2U));
	BTN_PORT->PUPDR &= ~(3U << (BTN_PIN * 2U));
	BTN_PORT->PUPDR |= (2U << (BTN_PIN * 2U));

	/* PG10: output (01), push-pull. */
	LED_PORT->MODER &= ~(3U << (LED_PIN * 2U));
	LED_PORT->MODER |= (1U << (LED_PIN * 2U));
	LED_PORT->OTYPER &= ~(1U << LED_PIN);
}

/* Read USER1 button state (active HIGH). */
static inline bool button_pressed(void)
{
	return (BTN_PORT->IDR & (1U << BTN_PIN)) != 0;
}

/* Set LED2 on/off. */
static inline void led_set(bool on)
{
	if (on) {
		LED_PORT->BSRR = (1U << LED_PIN);
	} else {
		LED_PORT->BSRR = (1U << (LED_PIN + 16U));
	}
}

/* ---- ST middleware user callback ---- */

/* Called by the middleware on host state transitions. */
static void usbh_user_process(USBH_HandleTypeDef *phost, uint8_t id)
{
	if (id == HOST_USER_CONNECTION) {
		LOG_INF("Keyboard connected");
	} else if (id == HOST_USER_DISCONNECTION) {
		LOG_INF("Keyboard disconnected");
	} else if (id == HOST_USER_CLASS_ACTIVE) {
		LOG_INF("Keyboard ready");
	}
}

/* ---- HID event callback ---- */

/* Called by the ST HID class driver when a new interrupt-IN report
 * arrives from the keyboard. Enqueues the report for the bridge. */
void USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
	HID_HandleTypeDef *hid;

	/* In mute mode, drop all reports. */
	if (!bridge_active) {
		return;
	}

	hid = (HID_HandleTypeDef *)phost->pActiveClass->pData;

	if (USBH_HID_GetDeviceType(phost) == HID_KEYBOARD &&
	    hid->length >= KB_REPORT_SIZE) {
		kbd_bridge_enqueue(hid->pData);
	}
}

/* ---- USBH process thread ---- */

#define USBH_STACK_SIZE 4096
#define USBH_PRIORITY   (CONFIG_MAIN_THREAD_PRIORITY + 1)

/* Background thread: runs the middleware state machine, polls the
 * button for mode toggle, and sends deferred LED updates. */
static void usbh_thread(void *p1, void *p2, void *p3)
{
	bool btn_prev = false;

	while (1) {
		/* Advance the middleware state machine. */
		USBH_Process(&hUSBHost);

		/* Edge-detect USER1 button (toggle on press, not hold). */
		bool btn_now = button_pressed();
		if (btn_now && !btn_prev) {
			bridge_active = !bridge_active;
			led_set(bridge_active);
			LOG_INF("Bridge %s", bridge_active ? "ON" : "OFF");
		}
		btn_prev = btn_now;

		/* Send deferred keyboard LED update.
		 * USBH_HID_SetReport is a multi-stage control transfer
		 * (SETUP → DATA → STATUS). Must be called repeatedly
		 * until it completes. */
		if (leds_dirty && hUSBHost.gState == HOST_CLASS) {
			static uint8_t led_buf;
			USBH_StatusTypeDef status;

			led_buf = pending_leds;
			leds_dirty = false;

			for (int i = 0; i < 200; i++) {
				status = USBH_HID_SetReport(&hUSBHost,
					0x02, 0x00, &led_buf, 1);
				if (status == USBH_OK || status == USBH_FAIL) {
					break;
				}
				USBH_Process(&hUSBHost);
				k_usleep(100);
			}
		}

		k_usleep(100);
	}
}

K_THREAD_STACK_DEFINE(usbh_stack, USBH_STACK_SIZE);
static struct k_thread usbh_thread_data;

/* ---- Public API ---- */

int usb_host_start(void)
{
	USBH_StatusTypeDef s;

	/* Initialize button and LED GPIO. */
	gpio_init();
	led_set(bridge_active);

	/* Initialize the ST USB Host Middleware.
	 * This calls USBH_LL_Init() → HAL_HCD_Init() → HAL_HCD_MspInit(). */
	s = USBH_Init(&hUSBHost, usbh_user_process, 0);
	if (s != USBH_OK) {
		return -EIO;
	}

	/* Register the HID class so keyboards are recognized. */
	s = USBH_RegisterClass(&hUSBHost, USBH_HID_CLASS);
	if (s != USBH_OK) {
		return -EIO;
	}

	/* Start the host — enables VBUS (PB9 HIGH) and begins
	 * watching for device connections. */
	s = USBH_Start(&hUSBHost);
	if (s != USBH_OK) {
		return -EIO;
	}

	/* Spawn the background thread for USBH_Process(). */
	k_thread_create(&usbh_thread_data, usbh_stack,
			K_THREAD_STACK_SIZEOF(usbh_stack),
			usbh_thread, NULL, NULL, NULL,
			USBH_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&usbh_thread_data, "usbh");

	return 0;
}

bool usb_host_bridge_active(void)
{
	return bridge_active;
}

/* Request a keyboard LED update (deferred to process thread). */
void usb_host_set_kbd_leds(uint8_t leds)
{
	pending_leds = leds;
	leds_dirty = true;
}
