/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup BspRiscvMbvValIntc
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
 * @defgroup BspRiscvMbvValIntc spec:/bsp/riscv/mbv/val/intc
 *
 * @ingroup TestsuitesBspsValidationBsp0
 *
 * @brief Tests the AXI Interrupt Controller support of the MicroBlaze V BSP.
 *
 * This test case performs the following actions:
 *
 * - Read the Master Enable Register.
 *
 *   - Check that the interrupt request output and the hardware interrupt
 *     inputs are enabled.
 *
 * - Look for a hardware input which is disabled and not pending.  With
 *   interrupts disabled, enable it, then disable it again.
 *
 *   - Check that enabling sets its bit in the Interrupt Enable Register and
 *     that rtems_interrupt_vector_is_enabled() reports it enabled.
 *
 *   - Check that disabling clears the bit again.
 *
 * - Check the error handling of the interrupt vector directives for a vector
 *   beyond the vector table.
 *
 * @{
 */

static bool FindDisabledInput( rtems_vector_number *vector )
{
  rtems_vector_number v;

  for ( v = 0; v < mbv_cfg.intc_num_intr_inputs; ++v ) {
    rtems_status_code sc;
    bool              enabled;
    bool              pending;

    sc = rtems_interrupt_vector_is_enabled( v, &enabled );
    T_rsc_success( sc );

    sc = rtems_interrupt_is_pending( v, &pending );
    T_rsc_success( sc );

    if ( !enabled && !pending ) {
      *vector = v;
      return true;
    }
  }

  return false;
}

/**
 * @brief Tests the AXI Interrupt Controller support of the MicroBlaze V BSP.
 */
T_TEST_CASE( BspRiscvMbvValIntc )
{
  rtems_interrupt_level level;
  rtems_vector_number   vector;
  rtems_status_code     sc;
  bool                  enabled;
  uint32_t              mask;

  T_eq_u32(
    MBV_INTC->mer,
    MICROBLAZE_INTC_MER_ME | MICROBLAZE_INTC_MER_HIE
  );

  T_assert_true( FindDisabledInput( &vector ) );
  mask = UINT32_C( 1 ) << vector;

  rtems_interrupt_local_disable( level );

  sc = rtems_interrupt_vector_enable( vector );
  T_rsc_success( sc );
  T_ne_u32( MBV_INTC->ier & mask, 0 );

  sc = rtems_interrupt_vector_is_enabled( vector, &enabled );
  T_rsc_success( sc );
  T_true( enabled );

  sc = rtems_interrupt_vector_disable( vector );
  T_rsc_success( sc );
  T_eq_u32( MBV_INTC->ier & mask, 0 );

  sc = rtems_interrupt_vector_is_enabled( vector, &enabled );
  T_rsc_success( sc );
  T_false( enabled );

  rtems_interrupt_local_enable( level );

  sc = rtems_interrupt_vector_enable( BSP_INTERRUPT_VECTOR_COUNT );
  T_rsc( sc, RTEMS_INVALID_ID );

  sc = rtems_interrupt_raise( BSP_INTERRUPT_VECTOR_COUNT );
  T_rsc( sc, RTEMS_INVALID_ID );
}

/** @} */
