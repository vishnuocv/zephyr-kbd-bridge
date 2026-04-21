/*
 * ST USB Host Middleware — HAL HCD callbacks and USBH_LL_* bridge.
 *
 * This file is boilerplate required by the ST middleware. It bridges
 * the middleware's abstract USBH_LL_* calls to the concrete HAL HCD
 * driver for USB2_OTG_HS.
 *
 * Hardware (STM32N6570-DK):
 *   USB2_OTG_HS: base 0x5806_0000, IRQ 178
 *   VBUS switch: PB9 → STMPS2151 (active HIGH, 100K pull-down)
 *   PHY clocks:  configured in main.c via LL API (not here)
 *   VddUSB:      enabled by Zephyr's udc_stm32 for USB1 (shared)
 */

#include "usbh_conf.h"
#include "usbh_core.h"
#include <zephyr/kernel.h>
#include <zephyr/irq.h>

/* HCD handle for OTG_HS2. */
HCD_HandleTypeDef hhcd_usb2;

/* ---- MSP init: clocks + IRQ ---- */

/* Called by HAL_HCD_Init(). Enables USB2 OTG and PHY clocks,
 * and connects the OTG_HS2 interrupt to Zephyr's IRQ subsystem. */
void HAL_HCD_MspInit(HCD_HandleTypeDef *hhcd)
{
	if (hhcd->Instance == USB2_OTG_HS) {
		__HAL_RCC_USB2_OTG_HS_CLK_ENABLE();
		__HAL_RCC_USB2_OTG_HS_PHY_CLK_ENABLE();

		IRQ_CONNECT(USB2_OTG_HS_IRQn, 7,
			    HAL_HCD_IRQHandler, &hhcd_usb2, 0);
		irq_enable(USB2_OTG_HS_IRQn);
	}
}

/* Called by HAL_HCD_DeInit(). Disables clocks and IRQ. */
void HAL_HCD_MspDeInit(HCD_HandleTypeDef *hhcd)
{
	if (hhcd->Instance == USB2_OTG_HS) {
		irq_disable(USB2_OTG_HS_IRQn);
		__HAL_RCC_USB2_OTG_HS_CLK_DISABLE();
		__HAL_RCC_USB2_OTG_HS_PHY_CLK_DISABLE();
	}
}

/* ---- HAL HCD ISR callbacks ---- */

/* SOF (Start of Frame): increments middleware timer for HID polling. */
void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hhcd)
{
	USBH_LL_IncTimer((USBH_HandleTypeDef *)hhcd->pData);
}

/* Device connect/disconnect. */
void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hhcd)
{
	USBH_LL_Connect((USBH_HandleTypeDef *)hhcd->pData);
}

void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hhcd)
{
	USBH_LL_Disconnect((USBH_HandleTypeDef *)hhcd->pData);
}

/* Port state changes. */
void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *hhcd)
{
	USBH_LL_PortEnabled((USBH_HandleTypeDef *)hhcd->pData);
}

void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hhcd)
{
	USBH_LL_PortDisabled((USBH_HandleTypeDef *)hhcd->pData);
}

/* URB state change — middleware polls via USBH_LL_GetURBState(). */
void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef *hhcd,
					  uint8_t chnum,
					  HCD_URBStateTypeDef urb_state)
{
}

/* ---- USBH_LL_* : middleware-to-HAL bridge ---- */

/* Initialize the HCD for USB2_OTG_HS. Called by USBH_Init(). */
USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *phost)
{
	hhcd_usb2.Instance = USB2_OTG_HS;
	hhcd_usb2.Init.Host_channels = 16;
	hhcd_usb2.Init.speed = HCD_SPEED_HIGH;
	hhcd_usb2.Init.dma_enable = DISABLE;
	hhcd_usb2.Init.phy_itface = USB_OTG_HS_EMBEDDED_PHY;
	hhcd_usb2.Init.Sof_enable = ENABLE;
	hhcd_usb2.Init.low_power_enable = DISABLE;
	hhcd_usb2.Init.vbus_sensing_enable = DISABLE;
	hhcd_usb2.Init.use_external_vbus = DISABLE;

	/* Cross-link middleware and HCD handles. */
	hhcd_usb2.pData = phost;
	phost->pData = &hhcd_usb2;

	/* HAL_HCD_Init calls MspInit + USB_CoreInit + USB_HostInit. */
	if (HAL_HCD_Init(&hhcd_usb2) != HAL_OK) {
		return USBH_FAIL;
	}

	USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&hhcd_usb2));
	return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *phost)
{
	HAL_HCD_DeInit(&hhcd_usb2);
	return USBH_OK;
}

/* Start/stop the host controller. */
USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *phost)
{
	HAL_HCD_Start(&hhcd_usb2);
	return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *phost)
{
	HAL_HCD_Stop(&hhcd_usb2);
	return USBH_OK;
}

/* Get the connected device's speed. */
USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *phost)
{
	switch (HAL_HCD_GetCurrentSpeed(&hhcd_usb2)) {
	case 0:  return USBH_SPEED_HIGH;
	case 1:  return USBH_SPEED_FULL;
	case 2:  return USBH_SPEED_LOW;
	default: return USBH_SPEED_FULL;
	}
}

/* Reset the USB port. */
USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef *phost)
{
	HAL_HCD_ResetPort(&hhcd_usb2);
	return USBH_OK;
}

/* Get the byte count of the last completed transfer. */
uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	return HAL_HCD_HC_GetXferCount(&hhcd_usb2, pipe);
}

/* Open a host channel (pipe) for a specific endpoint. */
USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *phost,
				     uint8_t pipe, uint8_t epnum,
				     uint8_t dev_address, uint8_t speed,
				     uint8_t ep_type, uint16_t mps)
{
	HAL_HCD_HC_Init(&hhcd_usb2, pipe, epnum, dev_address,
			speed, ep_type, mps);
	return USBH_OK;
}

/* Close (halt) a host channel. */
USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	HAL_HCD_HC_Halt(&hhcd_usb2, pipe);
	return USBH_OK;
}

/* Submit a USB request block (URB) to a host channel. */
USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *phost,
				      uint8_t pipe, uint8_t direction,
				      uint8_t ep_type, uint8_t token,
				      uint8_t *pbuff, uint16_t length,
				      uint8_t do_ping)
{
	HAL_HCD_HC_SubmitRequest(&hhcd_usb2, pipe, direction,
				 ep_type, token, pbuff, length, do_ping);
	return USBH_OK;
}

/* Get the current URB state for a host channel. */
USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *phost,
					  uint8_t pipe)
{
	return (USBH_URBStateTypeDef)HAL_HCD_HC_GetURBState(&hhcd_usb2, pipe);
}

/* Drive VBUS power to the USB-A port.
 * PB9 controls a STMPS2151 load switch (active HIGH).
 * Uses direct register writes to avoid linking HAL GPIO. */
USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *phost, uint8_t state)
{
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/* PB9: output, push-pull. */
	GPIOB->MODER &= ~(3U << (9U * 2U));
	GPIOB->MODER |= (1U << (9U * 2U));
	GPIOB->OTYPER &= ~(1U << 9U);

	/* Set or clear PB9. */
	if (state) {
		GPIOB->BSRR = (1U << 9U);
	} else {
		GPIOB->BSRR = (1U << (9U + 16U));
	}

	/* Wait for VBUS to stabilize. */
	k_msleep(200);
	return USBH_OK;
}

/* DATA0/DATA1 toggle tracking for reliable transfers. */
USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *phost,
				      uint8_t pipe, uint8_t toggle)
{
	HCD_HCTypeDef *hc = &hhcd_usb2.hc[pipe & 0xFU];

	if (hc->ep_is_in) {
		hc->toggle_in = toggle;
	} else {
		hc->toggle_out = toggle;
	}
	return USBH_OK;
}

uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *phost, uint8_t pipe)
{
	HCD_HCTypeDef *hc = &hhcd_usb2.hc[pipe & 0xFU];

	return hc->ep_is_in ? hc->toggle_in : hc->toggle_out;
}

/* Delay function mapped to Zephyr's scheduler. */
void USBH_Delay(uint32_t Delay)
{
	k_msleep(Delay);
}
