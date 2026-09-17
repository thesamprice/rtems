/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVESP32
 *
 * @brief ESP32 I2C master driver interface.
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

#ifndef LIBBSP_ESP32_BSP_SPI_H
#define LIBBSP_ESP32_BSP_SPI_H

#include <rtems.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers the GPSPI2 controller as a bus device.
 *
 * The bus appears at @a bus_path and is driven through <dev/spi/spi.h>, so a
 * device on it is reached with the ordinary open(), ioctl() and read()/write()
 * that any other RTEMS SPI bus offers.
 *
 * This is GPSPI2, the general purpose controller.  It is not SPI0 or SPI1,
 * which are the flash interface: driving those from here would be driving the
 * memory the program is executing from.
 *
 * The four pads are claimed through <bsp/pin.h>, so a configuration that has
 * already given one of them to I2C, UART1 or GPIO is refused rather than
 * quietly winning.
 *
 * @param bus_path The path of the bus device file, for example "/dev/spi-0".
 *
 * @retval RTEMS_SUCCESSFUL Successful operation.
 * @retval RTEMS_NO_MEMORY The bus could not be allocated.
 * @retval RTEMS_RESOURCE_IN_USE One of the pads belongs to another driver.
 * @retval RTEMS_UNSATISFIED The bus could not be registered at @a bus_path.
 */
rtems_status_code esp32c3_spi_register( const char *bus_path );

/**
 * @brief BSP-neutral names, for the same reason <bsp/i2c.h> has them.
 *
 * RTEMS has no convention for registering a SPI bus, so every BSP names the
 * call after its own chip and portable code has no way to ask for "this
 * board's SPI".  See #61.
 */
#define BSP_SPI_REGISTER esp32c3_spi_register
#define BSP_SPI_BUS_COUNT 1

#ifdef __cplusplus
}
#endif

#endif /* LIBBSP_ESP32_BSP_SPI_H */
