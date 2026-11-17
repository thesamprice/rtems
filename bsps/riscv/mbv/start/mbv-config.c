/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVMBV
 *
 * @brief Device configuration of the AMD MicroBlaze V generic BSP.
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

#include <bsp/mbv.h>

/*
 * The build system constants.  Every field of the structure has a value from
 * the moment the image is loaded, before any code runs, so a BSP which never
 * learns anything about its hardware at run time behaves exactly as if the
 * constants were used directly at each point of use.
 */
mbv_config mbv_cfg = {
  .intc = MBV_DEVICE( Microblaze_INTC, MBV_INTC_BASE ),
  .timer = MBV_DEVICE( Microblaze_Timer, MBV_TIMER_BASE ),
  .timer_2 = MBV_DEVICE( Microblaze_Timer, MBV_TIMER_2_BASE ),
  .uart_base = (uintptr_t) MBV_UART_BASE,
  .uart_16550_base = (uintptr_t) MBV_UART_16550_BASE,
  .timer_frequency = MBV_TIMER_FREQUENCY,
  .intc_kind_of_edge = MBV_INTC_KIND_OF_EDGE,
  .timer_irq = MBV_TIMER_IRQ,
  .timer_2_irq = MBV_TIMER_2_IRQ,
  .uart_irq = MBV_UART_IRQ,
  .uart_16550_irq = MBV_UART_16550_IRQ
};
