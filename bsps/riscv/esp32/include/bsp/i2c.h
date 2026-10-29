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

#ifndef LIBBSP_ESP32_BSP_I2C_H
#define LIBBSP_ESP32_BSP_I2C_H

#include <rtems.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers the I2C master controller as a bus device.
 *
 * The bus appears at @a bus_path and is driven through <dev/i2c/i2c.h>, so a
 * device on it is reached with the ordinary open(), ioctl() and read()/write()
 * that any other RTEMS I2C bus offers.
 *
 * The controller starts at 100 kHz.  Use the I2C_BUS_SET_CLOCK ioctl to change
 * it.
 *
 * @param bus_path The path of the bus device file, for example "/dev/i2c-0".
 *
 * @retval RTEMS_SUCCESSFUL Successful operation.
 * @retval RTEMS_NO_MEMORY The bus could not be allocated.
 * @retval RTEMS_INVALID_NUMBER The default clock could not be set.
 * @retval RTEMS_UNSATISFIED The bus could not be registered at @a bus_path.
 */
rtems_status_code esp32c3_i2c_register( const char *bus_path );

/**
 * @brief BSP-neutral names for what this BSP's I2C controller is called and
 *        how many of them there are.
 *
 * RTEMS has no convention for registering an I2C bus the way it has one for
 * the console, so every BSP that offers one names the call after its own
 * chip.  A consumer that wants "this board's I2C, whatever it is called" then
 * has no way to ask for it without naming the chip, which is exactly what
 * portable code must not do.
 *
 * Defining these here lets such a consumer test for <bsp/i2c.h> and use
 * BSP_I2C_REGISTER, with no chip name of its own.
 */
#define BSP_I2C_REGISTER esp32c3_i2c_register
#define BSP_I2C_BUS_COUNT 1

#ifdef __cplusplus
}
#endif

#endif /* LIBBSP_ESP32_BSP_I2C_H */
