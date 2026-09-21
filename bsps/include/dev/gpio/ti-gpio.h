/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSDeviceTIGPIO
 *
 * @brief This header file provides the interfaces of the Texas Instruments
 *   OMAP GPIO controller.
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

#ifndef LIBBSP_SHARED_DEV_GPIO_TI_GPIO_H
#define LIBBSP_SHARED_DEV_GPIO_TI_GPIO_H

#include <dev/gpio/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup RTEMSDeviceTIGPIO TI OMAP GPIO
 *
 * @ingroup RTEMSDeviceDrivers
 *
 * @brief This group contains the OMAP GPIO controller found on the Texas
 *   Instruments AM335x and its relatives.
 *
 * The part has several of these blocks, each 32 pins wide with its own
 * register base and its own interrupt, and each its own node in the device
 * tree.  One node becomes one controller and one device node, so an AM335x
 * BeagleBone Black has four of them and pin @a n of "/dev/gpio1" is what
 * the manual, the silkscreen and a Linux device tree all call GPIO1_@a n.
 *
 * @par Controller node
 *
 * The properties are the stock TI ones, so an unmodified board device tree
 * is enough to bring a controller up.
 *
 * - "compatible" is #TI_GPIO_COMPATIBLE.  A tree naming a later part, such
 *   as "ti,am4372-gpio", also lists this string, so it matches too.
 * - "reg" is the register base, translated through the "ranges" of every
 *   bus between the node and the root.  A mainline AM335x tree nests the
 *   controller inside a "ti,sysc" target module and its own "reg" is 0, so
 *   the walk is what produces 0x4804c000 rather than nothing.
 * - "interrupts" is read with the "#interrupt-cells" of the node's
 *   interrupt parent: one cell for the interrupt controller an AM335x tree
 *   names, three for a GIC.  Without it the controller still registers and
 *   rtems_gpio_pin_irq_enable() answers ENOTSUP.
 * - "status", where present and neither "okay" nor "ok", makes the search
 *   for a controller skip the node.
 * - "ngpios", from the standard GPIO binding, trims the pin count to the
 *   lines the board brings out.  It may not exceed #TI_GPIO_PIN_COUNT_MAX.
 *
 * @par Pin nodes
 *
 * Each child node annotates one pin.  A child with no "rtems,pin" is
 * ignored, and a pin with no child node still exists and is usable: the
 * children say what a pin is, they do not create it.  The names carry the
 * "rtems," vendor prefix because they extend a binding TI owns.  This is
 * the same binding the Xilinx Zynq driver uses.
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
 * @par What this block does not do
 *
 * The pull resistors and the slew rate of an AM335x pad are in the control
 * module and not here, the part has no open-drain or selectable drive
 * strength, and the debounce counter runs from a clock the power and clock
 * manager gates and this driver cannot turn on.  None of those are
 * reported as a capability, so rtems_gpio_pin_configure() refuses them
 * rather than accepting a configuration nothing below it would apply.
 *
 * A level trigger is reported and applied, but the block holds the status
 * bit up for as long as the level lasts, so a handler that does not remove
 * the cause before it returns is re-entered.  An edge trigger has no such
 * hazard.
 *
 * @par Example
 *
 * Two of an AM335x BeagleBone Black's four controllers, in the shape a
 * mainline am33xx-l4.dtsi node has.  They become "/dev/gpio1" and
 * "/dev/gpio2".
 *
 * @code{.unparsed}
 * target-module@4c000 {
 *     compatible = "ti,sysc-omap2", "ti,sysc";
 *     #address-cells = <1>;
 *     #size-cells = <1>;
 *     ranges = <0x0 0x4c000 0x1000>;
 *
 *     gpio1: gpio@0 {
 *         compatible = "ti,omap4-gpio";
 *         reg = <0x0 0x1000>;
 *         interrupts = <98>;
 *         gpio-controller;
 *         #gpio-cells = <2>;
 *         interrupt-controller;
 *         #interrupt-cells = <2>;
 *         status = "okay";
 *
 *         pin-usr0 {
 *             rtems,pin = <21>;
 *             rtems,label = "USR0";
 *         };
 *
 *         pin-emmc-clk {
 *             rtems,pin = <30>;
 *             rtems,label = "MMC1_CLK";
 *             rtems,owner = "emmc";
 *             rtems,reserved;
 *         };
 *     };
 * };
 *
 * target-module@ac000 {
 *     compatible = "ti,sysc-omap2", "ti,sysc";
 *     #address-cells = <1>;
 *     #size-cells = <1>;
 *     ranges = <0x0 0xac000 0x1000>;
 *
 *     gpio2: gpio@0 {
 *         compatible = "ti,omap4-gpio";
 *         reg = <0x0 0x1000>;
 *         interrupts = <32>;
 *         gpio-controller;
 *         #gpio-cells = <2>;
 *
 *         pin-sysboot0 {
 *             rtems,pin = <6>;
 *             rtems,label = "SYSBOOT0";
 *             rtems,strapping;
 *         };
 *     };
 * };
 * @endcode
 *
 * @{
 */

/**
 * @brief This constant contains the compatible string of the OMAP GPIO
 *   controller.
 */
#define TI_GPIO_COMPATIBLE "ti,omap4-gpio"

/**
 * @brief This constant represents the number of pins one of these blocks
 *   has.
 *
 * Every register in the block is one 32 bit word covering the whole block,
 * so this is a property of the hardware and not a limit this driver
 * imposes.
 */
#define TI_GPIO_PIN_COUNT_MAX 32

/**
 * @brief This constant represents the number of controllers this driver
 *   can register.
 *
 * The storage is static and sized by this.  An AM335x has four.
 */
#define TI_GPIO_CTRL_MAX 8

/**
 * @brief Registers one OMAP GPIO controller described by a flat device tree
 *   as a device node.
 *
 * The device tree is not copied and its strings are handed out as
 * rtems_gpio_pin_info::name and rtems_gpio_pin_info::owner, so @a fdt must
 * stay valid and unchanged for as long as the node exists.  Nothing the
 * pins are already driving is disturbed: the direction and output
 * registers are left as the boot loader wrote them and only the interrupt
 * registers are cleared.
 *
 * @param fdt is the flat device tree blob to read, usually bsp_fdt_get().
 *
 * @param node is the offset of the controller node in @a fdt, or a
 *   negative value to take the first enabled controller in the tree.
 *
 * @param path is the path of the device node, for example "/dev/gpio0".
 *
 * @retval 0 Successful operation.
 * @retval EINVAL @a fdt or @a path is NULL, @a fdt has no controller this
 *   driver recognises, or the node is malformed.
 * @retval EBUSY The node is already registered, or #TI_GPIO_CTRL_MAX
 *   controllers are.
 * @retval EIO The interrupt handler could not be installed.
 *
 * @return Returns an errno otherwise.
 */
int ti_gpio_register(const void* fdt, int node, const char* path);

/**
 * @brief Registers every enabled OMAP GPIO controller in a flat device tree
 *   as a device node.
 *
 * The controllers are numbered by ascending register base and the number is
 * appended to @a prefix, so a prefix of "/dev/gpio" gives an AM335x
 * "/dev/gpio0" through "/dev/gpio3" in the order the manual numbers the
 * blocks.  A board that wants some other mapping calls ti_gpio_register()
 * per node instead.
 *
 * Registering stops at the first failure, and the controllers registered
 * before it stay registered.
 *
 * @param fdt is the flat device tree blob to read, usually bsp_fdt_get().
 *
 * @param prefix is what the device node paths start with, for example
 *   "/dev/gpio".
 *
 * @retval 0 Successful operation.
 * @retval EINVAL @a fdt or @a prefix is NULL, @a fdt has no controller this
 *   driver recognises, or a node is malformed.
 * @retval EBUSY More than #TI_GPIO_CTRL_MAX controllers are enabled in
 *   @a fdt, or some are already registered.
 * @retval EIO An interrupt handler could not be installed.
 *
 * @return Returns an errno otherwise.
 */
int ti_gpio_register_all(const void* fdt, const char* prefix);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LIBBSP_SHARED_DEV_GPIO_TI_GPIO_H */
