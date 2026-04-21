/*
 * USB HID Keyboard Bridge — device side (OTG_HS1 → PC).
 *
 * Registers a boot-protocol HID keyboard with Zephyr's USBD-next stack
 * and runs a consumer loop that forwards reports from the message queue
 * to the PC via hid_device_submit_report().
 */

#include "kbd_bridge.h"
#include "usb_host.h"

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(kbd_bridge, LOG_LEVEL_INF);

/* Standard boot-protocol keyboard HID report descriptor. */
static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();

/* UDC static buffer for report submission (must be in DMA-safe memory). */
UDC_STATIC_BUF_DEFINE(submit_buf, KB_REPORT_SIZE);

/* Message queue: host callback produces, main thread consumes. */
K_MSGQ_DEFINE(kb_msgq, KB_REPORT_SIZE, 16, 4);

static const struct device *hid_dev;
static struct usbd_context *usbd_ctx;
static volatile bool hid_ready;
static uint32_t idle_duration;

/* ---- HID device callbacks ---- */

/* Called when the HID interface becomes ready/not ready. */
static void on_iface_ready(const struct device *dev, const bool ready)
{
	hid_ready = ready;
}

/* GET_REPORT: PC requests current keyboard state. */
static int on_get_report(const struct device *dev, const uint8_t type,
			 const uint8_t id, const uint16_t len,
			 uint8_t *const buf)
{
	if (len < KB_REPORT_SIZE) {
		return -ENOTSUP;
	}
	memset(buf, 0, KB_REPORT_SIZE);
	return KB_REPORT_SIZE;
}

/* SET_REPORT (output): PC sends LED state (NumLock, CapsLock, etc).
 * Forward to the physical keyboard via usb_host_set_kbd_leds(). */
static int on_set_report(const struct device *dev, const uint8_t type,
			 const uint8_t id, const uint16_t len,
			 const uint8_t *const buf)
{
	if (type != HID_REPORT_TYPE_OUTPUT || len < 1) {
		return -ENOTSUP;
	}
	usb_host_set_kbd_leds(buf[0]);
	return 0;
}

/* SET_IDLE / GET_IDLE: idle rate for report throttling. */
static void on_set_idle(const struct device *dev, const uint8_t id,
			const uint32_t duration)
{
	idle_duration = duration;
}

static uint32_t on_get_idle(const struct device *dev, const uint8_t id)
{
	return idle_duration;
}

/* SET_PROTOCOL: boot vs report protocol (we always use boot). */
static void on_set_protocol(const struct device *dev, const uint8_t proto)
{
}

/* OUTPUT_REPORT: alternative path for LED state on some hosts. */
static void on_output_report(const struct device *dev, const uint16_t len,
			     const uint8_t *const buf)
{
	on_set_report(dev, HID_REPORT_TYPE_OUTPUT, 0U, len, buf);
}

static struct hid_device_ops hid_ops = {
	.iface_ready   = on_iface_ready,
	.get_report    = on_get_report,
	.set_report    = on_set_report,
	.set_idle      = on_set_idle,
	.get_idle      = on_get_idle,
	.set_protocol  = on_set_protocol,
	.output_report = on_output_report,
};

/* ---- USBD message callback ---- */

/* Handles VBUS detection for boards with VBUS sensing. */
static void usbd_msg_cb(struct usbd_context *const ctx,
			const struct usbd_msg *const msg)
{
	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			usbd_enable(ctx);
		}
		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			usbd_disable(ctx);
		}
	}
}

/* ---- Public API ---- */

int kbd_bridge_init(void)
{
	int ret;

	/* Get the HID device node from the DTS overlay. */
	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(hid_dev)) {
		return -EIO;
	}

	/* Register boot-protocol keyboard descriptor and callbacks. */
	ret = hid_device_register(hid_dev, hid_report_desc,
				  sizeof(hid_report_desc), &hid_ops);
	if (ret) {
		return ret;
	}

	/* Initialize USBD context (enables VddUSB, configures USB1 PHY). */
	usbd_ctx = sample_usbd_init_device(usbd_msg_cb);
	return usbd_ctx ? 0 : -ENODEV;
}

int kbd_bridge_enable(void)
{
	if (!usbd_can_detect_vbus(usbd_ctx)) {
		return usbd_enable(usbd_ctx);
	}
	return 0;
}

int kbd_bridge_enqueue(const uint8_t report[KB_REPORT_SIZE])
{
	return k_msgq_put(&kb_msgq, report, K_NO_WAIT) ? -ENOMSG : 0;
}

void kbd_bridge_run(void)
{
	while (true) {
		uint8_t report[KB_REPORT_SIZE];

		/* Block until a report is available. */
		k_msgq_get(&kb_msgq, report, K_FOREVER);

		/* Drop if USB device not yet enumerated. */
		if (!hid_ready) {
			continue;
		}

		/* Copy to DMA-safe buffer and submit to PC. */
		memcpy(submit_buf, report, KB_REPORT_SIZE);
		hid_device_submit_report(hid_dev, KB_REPORT_SIZE, submit_buf);
	}
}
