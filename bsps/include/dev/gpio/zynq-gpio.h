/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSDeviceZynqGPIO
 *
 * @brief This header file provides the interfaces of the Xilinx Zynq and
 *   Zynq UltraScale+ MPSoC GPIO controller.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LIBBSP_SHARED_DEV_GPIO_ZYNQ_GPIO_H
#define LIBBSP_SHARED_DEV_GPIO_ZYNQ_GPIO_H

#include <dev/gpio/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup RTEMSDeviceZynqGPIO Xilinx Zynq GPIO
 *
 * @ingroup RTEMSDeviceDrivers
 *
 * @brief This group contains the GPIO controller of the Xilinx Zynq-7000
 *   and Zynq UltraScale+ MPSoC.
 *
 * One driver serves both parts.  The register block is the same and only
 * the geometry differs, so the geometry comes from the flat device tree
 * rather than from a preprocessor conditional: the compatible string picks
 * the bank layout, "reg" gives the register base and "interrupts" the
 * vector.
 *
 * @par Controller node
 *
 * The properties are the stock Xilinx ones, so an unmodified board device
 * tree is enough to bring the controller up.
 *
 * - "compatible" is #ZYNQ_GPIO_COMPATIBLE or #ZYNQMP_GPIO_COMPATIBLE and
 *   selects the bank layout: 118 pins in 4 banks, 54 of them MIO, for the
 *   first; 174 pins in 6 banks, 78 of them MIO, for the second.
 * - "reg" is the register base, read with the "#address-cells" and
 *   "#size-cells" of the parent bus, so a one cell Zynq tree and a two cell
 *   ZynqMP tree both work.
 * - "interrupts" is a three cell GIC specifier.  Without it the controller
 *   still registers and rtems_gpio_pin_irq_enable() answers ENOTSUP.
 * - "status", where present and neither "okay" nor "ok", makes the search
 *   for a controller skip the node.
 * - "ngpios", from the standard GPIO binding, trims the pin count to the
 *   lines the board brings out.  It may not exceed the part's.
 *
 * @par Pin nodes
 *
 * Each child node annotates one pin.  A child with no "rtems,pin" is
 * ignored, and a pin with no child node still exists and is usable: the
 * children say what a pin is, they do not create it.  The names carry the
 * "rtems," vendor prefix because they extend a binding Xilinx owns.
 *
 * - "rtems,pin" is a u32, the logical pin the node annotates.  Required.
 * - "rtems,label" is a string and becomes rtems_gpio_pin_info::name.
 * - "rtems,owner" is a string and becomes rtems_gpio_pin_info::owner.
 * - "rtems,reserved" is a boolean and sets #RTEMS_GPIO_PIN_RESERVED.
 * - "rtems,strapping" is a boolean and sets #RTEMS_GPIO_PIN_STRAPPING.
 * - "rtems,no-pad" is a boolean and sets #RTEMS_GPIO_PIN_NO_PAD.
 *
 * Direction, bias and trigger are deliberately not in the binding.  They
 * are a configuration rather than a property of the pin, and applying one
 * here would drive a pad without the capability and reservation checks
 * rtems_gpio_pin_configure() exists to make.
 *
 * @par Example
 *
 * @code{.unparsed}
 * gpio0: gpio@e000a000 {
 *     compatible = "xlnx,zynq-gpio-1.0";
 *     reg = <0xe000a000 0x1000>;
 *     interrupt-parent = <&intc>;
 *     interrupts = <0 20 4>;
 *     gpio-controller;
 *     #gpio-cells = <2>;
 *     status = "okay";
 *     ngpios = <62>;
 *
 *     pin-led0 {
 *         rtems,pin = <7>;
 *         rtems,label = "LED0";
 *     };
 *
 *     pin-flash-cs {
 *         rtems,pin = <1>;
 *         rtems,label = "QSPI_CS";
 *         rtems,owner = "qspi";
 *         rtems,reserved;
 *     };
 *
 *     pin-boot-mode {
 *         rtems,pin = <9>;
 *         rtems,label = "BOOT_MODE1";
 *         rtems,strapping;
 *     };
 * };
 * @endcode
 *
 * @{
 */

/**
 * @brief This constant contains the compatible string of the Zynq-7000
 *   GPIO controller.
 */
#define ZYNQ_GPIO_COMPATIBLE "xlnx,zynq-gpio-1.0"

/**
 * @brief This constant contains the compatible string of the Zynq
 *   UltraScale+ MPSoC GPIO controller.
 */
#define ZYNQMP_GPIO_COMPATIBLE "xlnx,zynqmp-gpio-1.0"

/**
 * @brief This constant represents the largest pin count of any part this
 *   driver supports, which is the Zynq UltraScale+ MPSoC.
 *
 * The controller's storage is static and sized by this, so a part added to
 * the driver with more pins has to raise it.
 */
#define ZYNQ_GPIO_PIN_COUNT_MAX 174

/**
 * @brief Registers a Zynq or Zynq UltraScale+ MPSoC GPIO controller
 *   described by a flat device tree as a device node.
 *
 * The device tree is not copied and its strings are handed out as
 * rtems_gpio_pin_info::name and rtems_gpio_pin_info::owner, so @a fdt must
 * stay valid and unchanged for as long as the node exists.  Only one
 * controller can be registered, since each part has one GPIO block.
 *
 * @param fdt is the flat device tree blob to read, usually bsp_fdt_get().
 *
 * @param node is the offset of the controller node in @a fdt, or a
 *   negative value to take the first enabled controller in the tree.
 *
 * @param path is the path of the device node, for example "/dev/gpio".
 *
 * @retval 0 Successful operation.
 * @retval EINVAL @a fdt or @a path is NULL, @a fdt has no controller this
 *   driver recognises, or the node is malformed.
 * @retval EBUSY A controller is already registered.
 * @retval EIO The interrupt handler could not be installed.
 *
 * @return Returns an errno otherwise.
 */
int zynq_gpio_register(const void* fdt, int node, const char* path);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LIBBSP_SHARED_DEV_GPIO_ZYNQ_GPIO_H */
