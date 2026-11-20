/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVMBV
 *
 * @brief Interrupt support for the AMD MicroBlaze V generic BSP.
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

#include <bsp/fatal.h>
#include <bsp/irq.h>
#include <bsp/irq-generic.h>
#include <bsp/mbv.h>

#include <rtems/score/percpu.h>
#include <rtems/score/riscv-utility.h>

RTEMS_INTERRUPT_LOCK_DEFINE( static, mbv_intc_lock, "AXI INTC" )

static uint32_t mbv_intc_input_mask( rtems_vector_number vector )
{
  return UINT32_C( 1 ) << vector;
}

void _RISCV_Interrupt_dispatch( uintptr_t mcause, Per_CPU_Control *cpu_self )
{
  volatile Microblaze_INTC *intc;
  uint32_t                  kind_of_edge;
  uint32_t                  pending;

  (void) cpu_self;

  /*
   * Get rid of the most significant bit which indicates if the exception was
   * caused by an interrupt or not.  The Machine External Interrupt is the
   * only interrupt of MicroBlaze V in M-mode.
   */
  mcause <<= 1;

  if ( mcause != ( RISCV_INTERRUPT_EXTERNAL_MACHINE << 1 ) ) {
    bsp_fatal( RISCV_FATAL_UNEXPECTED_INTERRUPT_EXCEPTION );
  }

  /*
   * Hoist the configuration into locals.  This is the per-interrupt hot path
   * and the values do not change after the system initialization.
   */
  intc = mbv_cfg.intc;
  kind_of_edge = mbv_cfg.intc_kind_of_edge;

  /* The IPR is optional (C_HAS_IPR), so mask the ISR with the IER instead */
  while ( ( pending = ( intc->isr & intc->ier ) ) != 0 ) {
    rtems_vector_number vector;
    uint32_t            mask;

    vector = (rtems_vector_number) __builtin_ctz( pending );
    mask = mbv_intc_input_mask( vector );

    if ( ( kind_of_edge & mask ) != 0 ) {
      /*
       * Edge-triggered input: acknowledge before the handler runs so that a
       * new edge occurring while the handler executes is latched again.
       */
      intc->iar = mask;
      bsp_interrupt_handler_dispatch( vector );
    } else {
      /*
       * Level-sensitive input: the handler clears the interrupt condition at
       * the device, acknowledge afterwards.
       */
      bsp_interrupt_handler_dispatch( vector );
      intc->iar = mask;
    }
  }
}

void bsp_interrupt_facility_initialize( void )
{
  volatile Microblaze_INTC *intc = MBV_INTC;

  /* Disable and acknowledge all interrupt controller inputs */
  intc->ier = 0;
  intc->iar = 0xffffffff;

  /*
   * Enable the interrupt request output and the hardware interrupt inputs.
   * The HIE bit is write-once, so after this the ISR bits of the hardware
   * inputs can no longer be set by software.
   */
  intc->mer = MICROBLAZE_INTC_MER_ME | MICROBLAZE_INTC_MER_HIE;

  /* Enable the machine external interrupt on the hart */
  set_csr( mie, MIP_MEIP );
}

bool bsp_interrupt_is_valid_vector( rtems_vector_number vector )
{
  return vector < (rtems_vector_number) BSP_INTERRUPT_VECTOR_COUNT;
}

rtems_status_code bsp_interrupt_get_attributes(
  rtems_vector_number         vector,
  rtems_interrupt_attributes *attributes
)
{
  (void) vector;

  attributes->is_maskable = true;
  attributes->can_enable = true;
  attributes->maybe_enable = true;
  attributes->can_disable = true;
  attributes->maybe_disable = true;
  attributes->cleared_by_acknowledge = true;
  attributes->can_clear = true;

  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_is_pending(
  rtems_vector_number vector,
  bool               *pending
)
{
  bsp_interrupt_assert( bsp_interrupt_is_valid_vector( vector ) );
  bsp_interrupt_assert( pending != NULL );

  *pending = ( MBV_INTC->isr & mbv_intc_input_mask( vector ) ) != 0;
  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_raise( rtems_vector_number vector )
{
  bsp_interrupt_assert( bsp_interrupt_is_valid_vector( vector ) );
  (void) vector;

  return RTEMS_UNSATISFIED;
}

rtems_status_code bsp_interrupt_clear( rtems_vector_number vector )
{
  bsp_interrupt_assert( bsp_interrupt_is_valid_vector( vector ) );

  MBV_INTC->iar = mbv_intc_input_mask( vector );
  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_vector_is_enabled(
  rtems_vector_number vector,
  bool               *enabled
)
{
  bsp_interrupt_assert( bsp_interrupt_is_valid_vector( vector ) );
  bsp_interrupt_assert( enabled != NULL );

  *enabled = ( MBV_INTC->ier & mbv_intc_input_mask( vector ) ) != 0;
  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_vector_enable( rtems_vector_number vector )
{
  rtems_interrupt_lock_context lock_context;

  bsp_interrupt_assert( bsp_interrupt_is_valid_vector( vector ) );

  rtems_interrupt_lock_acquire( &mbv_intc_lock, &lock_context );
  MBV_INTC->ier |= mbv_intc_input_mask( vector );
  rtems_interrupt_lock_release( &mbv_intc_lock, &lock_context );
  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_vector_disable( rtems_vector_number vector )
{
  rtems_interrupt_lock_context lock_context;

  bsp_interrupt_assert( bsp_interrupt_is_valid_vector( vector ) );

  rtems_interrupt_lock_acquire( &mbv_intc_lock, &lock_context );
  MBV_INTC->ier &= ~mbv_intc_input_mask( vector );
  rtems_interrupt_lock_release( &mbv_intc_lock, &lock_context );
  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_set_priority(
  rtems_vector_number vector,
  uint32_t            priority
)
{
  bsp_interrupt_assert( bsp_interrupt_is_valid_vector( vector ) );
  (void) vector;
  (void) priority;

  return RTEMS_UNSATISFIED;
}

rtems_status_code bsp_interrupt_get_priority(
  rtems_vector_number vector,
  uint32_t           *priority
)
{
  bsp_interrupt_assert( bsp_interrupt_is_valid_vector( vector ) );
  bsp_interrupt_assert( priority != NULL );
  (void) vector;
  (void) priority;

  return RTEMS_UNSATISFIED;
}
