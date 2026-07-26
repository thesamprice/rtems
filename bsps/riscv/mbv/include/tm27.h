/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVMBV
 *
 * @brief Time Test 27 support.
 *
 * The interrupt controller does not support software-raised interrupts once
 * the hardware interrupt enable (HIE) bit is set, so the second AXI Timer of
 * the platform is used as a software-controlled interrupt source.
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

#ifndef _RTEMS_TMTEST27
#error "This is an RTEMS internal file you must not include directly."
#endif

#ifndef __tm27_h
#define __tm27_h

#include <bsp.h>
#include <bsp/irq.h>
#include <bsp/mbv.h>

#include <rtems/irq-extension.h>
#include <rtems/score/isrlevel.h>

#define MUST_WAIT_FOR_INTERRUPT 1

/*
 * The second dual-channel AXI Timer of the QEMU amd-microblaze-v-generic
 * machine, connected to interrupt controller input 6.  It is not used by the
 * clock driver, so tests may program it freely.
 */
#define TM27_TIMER ((volatile Microblaze_Timer *) 0x41c10000)

#define TM27_INTERRUPT_VECTOR MBV_IRQ_TIMER_1

static inline void Install_tm27_vector( rtems_interrupt_handler handler )
{
  volatile Microblaze_Timer *timer = TM27_TIMER;

  /* Stop the timer and clear a pending interrupt */
  timer->tcsr0 = MICROBLAZE_TIMER_TCSR0_T0INT;

  /* Expire almost immediately once the timer is started */
  timer->tlr0 = 1;

  (void) rtems_interrupt_handler_install(
    TM27_INTERRUPT_VECTOR,
    "tm27",
    RTEMS_INTERRUPT_UNIQUE,
    handler,
    NULL
  );
}

static inline void Cause_tm27_intr( void )
{
  volatile Microblaze_Timer *timer = TM27_TIMER;

  /* Load the counter and start a one-shot down count */
  timer->tcsr0 = MICROBLAZE_TIMER_TCSR0_LOAD0;
  timer->tcsr0 = MICROBLAZE_TIMER_TCSR0_ENIT0 | MICROBLAZE_TIMER_TCSR0_UDT0 |
    MICROBLAZE_TIMER_TCSR0_ENT0;
}

static inline void Clear_tm27_intr( void )
{
  volatile Microblaze_Timer *timer = TM27_TIMER;

  /* Stop the timer and clear the interrupt */
  timer->tcsr0 = MICROBLAZE_TIMER_TCSR0_T0INT;
}

static inline void Lower_tm27_intr( void )
{
  /*
   * The interrupt raised by Cause_tm27_intr() is level-sensitive and remains
   * pending in the interrupt controller, so enabling interrupts is
   * sufficient to take it.
   */
  _ISR_Set_level( 0 );
}

#endif /* __tm27_h */
