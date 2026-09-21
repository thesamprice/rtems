/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVESP32C3GPIO
 *
 * @brief This header file provides the interfaces of the ESP32-C3 GPIO
 *   controller.
 */

/*
 * Copyright (C) 2026 Samuel Price
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LIBBSP_RISCV_ESP32_BSP_ESP32C3_GPIO_H
#define LIBBSP_RISCV_ESP32_BSP_ESP32C3_GPIO_H

#include <dev/gpio/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup RTEMSBSPsRISCVESP32C3GPIO ESP32-C3 GPIO
 *
 * @ingroup RTEMSBSPsRISCVESP32
 *
 * @brief This group contains the ESP32-C3 GPIO controller.
 *
 * Which pads an application may drive comes from three places, none of them
 * a setting.  The SPI flash pads and the strapping pads are properties of
 * the part.  The console pair follows ESPRESSIF_USE_USB_CONSOLE.  Pads held
 * by another driver come from bsp_pin_claim(), where the BSP has it, so that
 * answer cannot fall out of date with what is linked in.
 *
 * @{
 */

/**
 * @brief This constant represents the number of GPIO pads the ESP32-C3
 *   package brings out.
 */
#define ESP32C3_GPIO_PIN_COUNT 22

/**
 * @brief Registers the ESP32-C3 GPIO controller as a device node.
 *
 * @param path is the path of the device node, for example "/dev/gpio0".
 *
 * @retval 0 Successful operation.
 * @retval EINVAL The controller is not usable.
 * @retval EEXIST The path already exists.
 *
 * @return Returns an errno otherwise.
 */
int esp32c3_gpio_register( const char *path );

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LIBBSP_RISCV_ESP32_BSP_ESP32C3_GPIO_H */
