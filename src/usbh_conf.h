/*
 * ST USB Host Middleware — compile-time configuration.
 *
 * Defines resource limits, memory functions, and debug level
 * for the middleware. Also provides the HCD handle declaration.
 */

#ifndef USBH_CONF_H
#define USBH_CONF_H

#include <stm32n6xx_hal.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Resource limits for HID keyboard use case. */
#define USBH_MAX_NUM_ENDPOINTS       2    /* EP0 + 1 interrupt-IN */
#define USBH_MAX_NUM_INTERFACES      2
#define USBH_MAX_NUM_CONFIGURATION   1
#define USBH_MAX_NUM_SUPPORTED_CLASS 1    /* HID only */
#define USBH_MAX_SIZE_CONFIGURATION  256
#define USBH_MAX_DATA_BUFFER         512

/* Debug: 0=off, 1=errors, 2=warnings, 3=all. */
#define USBH_DEBUG_LEVEL 0

/* Map middleware memory functions to Zephyr. */
#define USBH_malloc   k_malloc
#define USBH_free     k_free
#define USBH_memset   memset
#define USBH_memcpy   memcpy

/* Debug printf mapping (only used if USBH_DEBUG_LEVEL > 0). */
#if (USBH_DEBUG_LEVEL > 0U)
#define USBH_UsrLog(...)   do { printf("USBH USR: "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBH_UsrLog(...)
#endif

#if (USBH_DEBUG_LEVEL > 1U)
#define USBH_ErrLog(...)   do { printf("USBH ERR: "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBH_ErrLog(...)
#endif

#if (USBH_DEBUG_LEVEL > 2U)
#define USBH_DbgLog(...)   do { printf("USBH DBG: "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define USBH_DbgLog(...)
#endif

/* HCD handle for OTG_HS2 (defined in usbh_conf.c). */
extern HCD_HandleTypeDef hhcd_usb2;

#endif
