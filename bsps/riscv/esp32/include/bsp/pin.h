/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVESP32
 *
 * @brief Pin ownership for the ESP32-C3.
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

#ifndef LIBBSP_RISCV_ESP32_BSP_PIN_H
#define LIBBSP_RISCV_ESP32_BSP_PIN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The number of pads this part has, GPIO0 through GPIO21.
 */
#define BSP_PIN_COUNT 22

/**
 * @brief Claims @a pin for @a owner.
 *
 * Every driver in this BSP that points a pad at its peripheral -- I2C, UART1
 * and the GPIO driver all do -- claims the pad first, so that the second
 * driver to want it is told rather than silently winning.  Two of them
 * writing IO_MUX and the GPIO matrix for the same pad leaves whichever ran
 * last in control, and on a board with no pads attached, which is every QEMU
 * run, the collision produces no symptom at all.
 *
 * This deliberately does not go through rtems_gpio_request_pin(), which does
 * the same bookkeeping and more.  Reaching it means linking gpio-support.c,
 * a little over 2000 lines with a mutex per bank and an optional interrupt
 * server task, into every application that merely has an I2C bus, on a part
 * with 320 KiB of RAM.  This table is a bitmask and an array of pointers.
 *
 * @param pin The pad, 0 to BSP_PIN_COUNT - 1.
 * @param owner A string naming the claimant, used only for diagnostics.  It
 *   must have static storage duration; nothing copies it.
 *
 * @retval true The pad is now @a owner's.  Claiming a pad that @a owner
 *   already holds succeeds and changes nothing, so a driver that configures
 *   a pad in more than one step need not track whether it has claimed yet.
 * @retval false @a pin is out of range, or another owner holds it.  The
 *   caller should fail rather than carry on: the pad is not its to configure.
 */
bool bsp_pin_claim( uint32_t pin, const char *owner );

/**
 * @brief Releases @a pin.
 *
 * Does not restore the pad's configuration -- that is the caller's, since
 * only it knows what the pad should become.  Releasing a pad nobody holds is
 * allowed and does nothing.
 */
void bsp_pin_release( uint32_t pin );

/**
 * @brief The owner of @a pin, or NULL if it is free or out of range.
 *
 * For diagnostics.  A driver that has just been refused a pad can say who
 * has it, which is the difference between a usable error message and
 * "failed".
 */
const char *bsp_pin_owner( uint32_t pin );

#ifdef __cplusplus
}
#endif

#endif /* LIBBSP_RISCV_ESP32_BSP_PIN_H */
