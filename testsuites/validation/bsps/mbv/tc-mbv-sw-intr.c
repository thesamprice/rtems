/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup BspRiscvMbvValSwIntr
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <bsp.h>
#include <bsp/irq.h>
#include <bsp/mbv.h>
#include <rtems/irq-extension.h>

#include <rtems/test.h>

/**
 * @defgroup BspRiscvMbvValSwIntr spec:/bsp/riscv/mbv/val/sw-intr
 *
 * @ingroup TestsuitesBspsValidationBsp0
 *
 * @brief Tests the software interrupts of the AXI Interrupt Controller.
 *
 * This test case performs the following actions:
 *
 * - Get the attributes of the hardware inputs and the software interrupts.
 *
 *   - Check that exactly the software interrupts can be raised.
 *
 *   - Check that raising a hardware input fails.
 *
 * - Install a handler on the last software interrupt and raise it.
 *
 *   - Check that the handler ran exactly once and that the interrupt is no
 *     longer pending.
 *
 * - Raise the software interrupt with interrupts disabled, then clear it and
 *   enable interrupts.
 *
 *   - Check that the interrupt was pending after the raise and not after the
 *     clear, and that the handler did not run.
 *
 * @{
 */

static volatile uint32_t handler_calls;

static void Handler( void *arg )
{
  (void) arg;
  ++handler_calls;
}

static bool IsPending( rtems_vector_number vector )
{
  rtems_status_code sc;
  bool              pending;

  sc = rtems_interrupt_is_pending( vector, &pending );
  T_rsc_success( sc );

  return pending;
}

/**
 * @brief Tests the software interrupts of the AXI Interrupt Controller.
 */
T_TEST_CASE( BspRiscvMbvValSwIntr )
{
  rtems_interrupt_attributes attr;
  rtems_interrupt_level      level;
  rtems_vector_number        first;
  rtems_vector_number        vector;
  rtems_status_code          sc;
  uint32_t                   i;

  T_assert_gt_u32( mbv_cfg.intc_num_sw_intr, 0 );

  first = MBV_INTERRUPT_VECTOR_EXTERNAL( mbv_cfg.intc_num_intr_inputs );

  for ( vector = 0; vector < first + mbv_cfg.intc_num_sw_intr; ++vector ) {
    sc = rtems_interrupt_get_attributes( vector, &attr );
    T_rsc_success( sc );
    T_eq( attr.can_raise, vector >= first );
  }

  sc = rtems_interrupt_raise( MBV_INTERRUPT_VECTOR_EXTERNAL( 0 ) );
  T_rsc( sc, RTEMS_UNSATISFIED );

  vector = first + mbv_cfg.intc_num_sw_intr - 1;
  handler_calls = 0;

  sc = rtems_interrupt_handler_install(
    vector,
    "SwIntr",
    RTEMS_INTERRUPT_SHARED,
    Handler,
    NULL
  );
  T_rsc_success( sc );

  sc = rtems_interrupt_raise( vector );
  T_rsc_success( sc );

  for ( i = 0; i < 1000000 && handler_calls == 0; ++i ) {
    /* Wait for the interrupt */
  }

  T_eq_u32( handler_calls, 1 );
  T_false( IsPending( vector ) );

  rtems_interrupt_local_disable( level );
  sc = rtems_interrupt_raise( vector );
  T_rsc_success( sc );
  T_true( IsPending( vector ) );
  sc = rtems_interrupt_clear( vector );
  T_rsc_success( sc );
  T_false( IsPending( vector ) );
  rtems_interrupt_local_enable( level );

  T_eq_u32( handler_calls, 1 );

  sc = rtems_interrupt_handler_remove( vector, Handler, NULL );
  T_rsc_success( sc );
}

/** @} */
