/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVMBV
 *
 * @brief Device definitions for the AMD MicroBlaze V generic platform.
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

#ifndef LIBBSP_RISCV_MBV_MBV_H
#define LIBBSP_RISCV_MBV_MBV_H

#include <bsp.h>

/*
 * The AXI Interrupt Controller and AXI Timer register blocks are identical to
 * the ones used by the MicroBlaze BSPs, so the register definitions are
 * shared with bsps/microblaze.
 */
#include <bsp/intc.h>
#include <bsp/microblaze-timer.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The device base addresses are plain integer constants defined by the build
 * system.  Cast them through uintptr_t so that the device pointers are formed
 * without an int to pointer conversion of a different width on RV64.
 */
#define MBV_DEVICE(type, base) ((volatile type *) (uintptr_t) (base))

/**
 * @brief The device configuration of the platform.
 *
 * MicroBlaze V is a soft processor.  Where a peripheral is placed, which
 * interrupt controller input it drives, and how fast the timer input clock
 * runs are properties of a particular Vivado design and not of the
 * architecture.  Holding them in one structure instead of in preprocessor
 * constants lets the BSP take them from a device tree at run time.
 *
 * The structure is statically initialized with the build system constants, so
 * a BSP which finds no device tree behaves exactly as if the constants were
 * used directly.
 */
typedef struct {
  /**
   * @brief The AXI Interrupt Controller of the platform.
   *
   * Its interrupt output is wired to the Machine External Interrupt (MEIP) of
   * the hart.  There is no PLIC and no CLINT on this platform.
   */
  volatile Microblaze_INTC *intc;

  /**
   * @brief The dual-channel AXI Timer of the platform.
   *
   * Channel 0 generates the clock tick via interrupt controller input
   * #timer_irq.  Channel 1 is used as a free-running counter for the
   * timecounter and the CPU counter.
   */
  volatile Microblaze_Timer *timer;

  /**
   * @brief The second dual-channel AXI Timer of the platform.
   *
   * Channel 0 is used as the software-raised interrupt source through
   * interrupt controller input #timer_2_irq, see bsp_interrupt_raise() and
   * the Time Test 27 support.  Channel 1 is unused.
   */
  volatile Microblaze_Timer *timer_2;

  /** @brief The register base address of the AXI UART Lite console device. */
  uintptr_t uart_base;

  /** @brief The register base address of the 16550 UART. */
  uintptr_t uart_16550_base;

  /** @brief The input clock frequency of both AXI Timers in Hz. */
  uint32_t timer_frequency;

  /**
   * @brief The bit mask of edge-triggered interrupt controller inputs.
   *
   * This is the kind-of-intr configuration of the AXI Interrupt Controller.
   * A set bit selects edge-triggered, a clear bit level-sensitive.
   */
  uint32_t intc_kind_of_edge;

  /** @brief The interrupt controller input of #timer channel 0. */
  uint32_t timer_irq;

  /** @brief The interrupt controller input of #timer_2 channel 0. */
  uint32_t timer_2_irq;

  /** @brief The interrupt controller input of the AXI UART Lite. */
  uint32_t uart_irq;

  /** @brief The interrupt controller input of the 16550 UART. */
  uint32_t uart_16550_irq;
} mbv_config;

/**
 * @brief The device configuration in use by this BSP.
 */
extern mbv_config mbv_cfg;

#define MBV_INTC (mbv_cfg.intc)

#define MBV_TIMER (mbv_cfg.timer)

#define MBV_TIMER_2 (mbv_cfg.timer_2)

#ifdef __cplusplus
}
#endif

#endif /* LIBBSP_RISCV_MBV_MBV_H */
