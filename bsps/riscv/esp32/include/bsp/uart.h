/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVESP32
 *
 * @brief ESP32 UART driver interface.
 */

/*
 * Copyright (C) 2026 Samuel Price <thesamprice@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef LIBBSP_ESP32_BSP_UART_H
#define LIBBSP_ESP32_BSP_UART_H

#include <rtems.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of general-purpose UARTs on this chip.
 */
#define ESP32C3_UART_COUNT 2

/**
 * @brief Registers a UART as a termios device.
 *
 * The port appears at @a device_path and is used with the ordinary open(),
 * read(), write() and tcsetattr(); nothing above it needs to know what chip
 * it is on.
 *
 * This is not the console.  console-config.c drives UART0 through the boot
 * ROM's routines so that printk() works before anything is initialised, and
 * that path is not an openable device.  Registering port 0 here is allowed and
 * deliberately does not reprogram the port, so a console on it keeps working;
 * @a baud is then taken as a statement of what it is already set to rather
 * than a request to change it.
 *
 * @param device_path Path of the device file, for example "/dev/ttyS1".
 * @param port UART number, 0 or 1.
 * @param baud Bit rate.
 *
 * @retval RTEMS_SUCCESSFUL Successful operation.
 * @retval RTEMS_INVALID_NUMBER @a port does not exist, or @a baud cannot be
 *   produced from the peripheral clock.
 * @retval RTEMS_UNSATISFIED The device file could not be created.
 */
rtems_status_code esp32c3_uart_register(
  const char *device_path,
  unsigned    port,
  uint32_t    baud
);

/**
 * @brief BSP-neutral names for this BSP's UART registration and how many
 *        ports it has.
 *
 * RTEMS has no convention for registering a serial port that is not the
 * console, so every BSP names the call after its own chip.  These let a
 * portable consumer test for <bsp/uart.h> and use BSP_UART_REGISTER without
 * naming one.  The same reasoning as <bsp/i2c.h>'s BSP_I2C_REGISTER.
 */
#define BSP_UART_REGISTER esp32c3_uart_register
#define BSP_UART_PORT_COUNT ESP32C3_UART_COUNT

#ifdef __cplusplus
}
#endif

#endif /* LIBBSP_ESP32_BSP_UART_H */
