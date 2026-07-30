/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVMBV
 *
 * @brief Time Test 27 support.
 *
 * The interrupt controller does not support software-raised interrupts once
 * the hardware interrupt enable (HIE) bit is set, so an otherwise unused
 * peripheral has to act as the software-controlled interrupt source.  The
 * BSP provides two of them, see bsp_interrupt_raise():  the transmitter
 * holding register empty interrupt of the 16550 UART and channel 0 of the
 * second AXI Timer.
 *
 * This support uses the 16550, because it is the more immediate of the two:
 * the register write which enables the interrupt asserts the interrupt
 * controller input as part of the very same bus access, so the request is
 * pending by the time Cause_tm27_intr() returns.  The timer needs one timer
 * clock to underflow, which real hardware delivers within a few processor
 * cycles but an emulator may take much longer to model.
 *
 * This leaves the timer as the raisable interrupt vector without installed
 * entries which GetTestableInterruptVector() of the validation test suites
 * searches for.  That search runs before the TM27_INTERRUPT_VECTOR_ALTERNATIVE
 * fallback and always succeeds here, which is why no alternative interrupt
 * request is defined below.
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
 * The interrupt controller input of the 16550 UART is part of the device
 * configuration, so this is not a constant expression.  All users evaluate it
 * at run time.
 */
#define TM27_INTERRUPT_VECTOR \
  MBV_INTERRUPT_VECTOR_EXTERNAL( mbv_cfg.uart_16550_irq )

/*
 * Tell the validation test suites which interrupt vector Cause_tm27_intr()
 * raises, instead of making them search the interrupt vector table for the
 * handler installed by Install_tm27_vector().
 */
#define TM27_INTERRUPT_VECTOR_DEFAULT TM27_INTERRUPT_VECTOR

static inline void Install_tm27_vector( rtems_interrupt_handler handler )
{
  (void) rtems_interrupt_handler_install(
    TM27_INTERRUPT_VECTOR,
    "tm27",
    RTEMS_INTERRUPT_SHARED,
    handler,
    NULL
  );
}

static inline void Cause_tm27_intr( void )
{
  (void) rtems_interrupt_raise( TM27_INTERRUPT_VECTOR );
}

static inline void Clear_tm27_intr( void )
{
  (void) rtems_interrupt_clear( TM27_INTERRUPT_VECTOR );
}

static inline void Lower_tm27_intr( void )
{
  /*
   * The raised interrupt remains pending in the interrupt controller, so
   * enabling interrupts is sufficient to take it.
   */
  _ISR_Set_level( 0 );
}

#endif /* __tm27_h */
