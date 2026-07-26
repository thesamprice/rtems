/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVMBV
 *
 * @brief Interrupt support for the AMD MicroBlaze V generic BSP.
 *
 * The single hart has no PLIC and no CLINT.  All peripheral interrupts are
 * routed through a Xilinx AXI Interrupt Controller whose output drives the
 * Machine External Interrupt (MEIP) of the hart.  The interrupt controller
 * inputs 0..31 are mapped to the RTEMS interrupt vectors
 * MBV_INTERRUPT_VECTOR_EXTERNAL(0..31).
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

RTEMS_INTERRUPT_LOCK_DEFINE(static, mbv_intc_lock, "AXI INTC")

void _RISCV_Interrupt_dispatch(uintptr_t mcause, Per_CPU_Control *cpu_self)
{
  (void) cpu_self;

  /*
   * Get rid of the most significant bit which indicates if the exception was
   * caused by an interrupt or not.
   */
  mcause <<= 1;

  if (mcause == (RISCV_INTERRUPT_EXTERNAL_MACHINE << 1)) {
    uint32_t pending;

    while ((pending = (MBV_INTC->isr & MBV_INTC->ier)) != 0) {
      uint32_t index = (uint32_t) __builtin_ctz(pending);
      uint32_t mask = UINT32_C(1) << index;

      if ((MBV_INTC_KIND_OF_EDGE & mask) != 0) {
        /*
         * Edge-triggered input: acknowledge before the handler runs so that
         * a new edge occurring while the handler executes is latched again.
         */
        MBV_INTC->iar = mask;
        bsp_interrupt_handler_dispatch(MBV_INTERRUPT_VECTOR_EXTERNAL(index));
      } else {
        /*
         * Level-sensitive input: the handler clears the interrupt condition
         * at the device, acknowledge afterwards.
         */
        bsp_interrupt_handler_dispatch(MBV_INTERRUPT_VECTOR_EXTERNAL(index));
        MBV_INTC->iar = mask;
      }
    }
  } else if (mcause == (RISCV_INTERRUPT_TIMER_MACHINE << 1)) {
    bsp_interrupt_handler_dispatch_unchecked(MBV_INTERRUPT_VECTOR_TIMER);
  } else if (mcause == (RISCV_INTERRUPT_SOFTWARE_MACHINE << 1)) {
    bsp_interrupt_handler_dispatch_unchecked(MBV_INTERRUPT_VECTOR_SOFTWARE);
  } else {
    bsp_fatal(RISCV_FATAL_UNEXPECTED_INTERRUPT_EXCEPTION);
  }
}

void bsp_interrupt_facility_initialize(void)
{
  /* Disable and acknowledge all interrupt controller inputs */
  MBV_INTC->ier = 0;
  MBV_INTC->iar = 0xffffffff;

  /* Enable hardware interrupts on the interrupt controller */
  MBV_INTC->mer = MICROBLAZE_INTC_MER_ME | MICROBLAZE_INTC_MER_HIE;

  /* Enable the machine external interrupt on the hart */
  set_csr(mie, MIP_MEIP);
}

bool bsp_interrupt_is_valid_vector(rtems_vector_number vector)
{
  return vector < (rtems_vector_number) BSP_INTERRUPT_VECTOR_COUNT;
}

rtems_status_code bsp_interrupt_get_attributes(
  rtems_vector_number         vector,
  rtems_interrupt_attributes *attributes
)
{
  bool is_external = MBV_INTERRUPT_VECTOR_IS_EXTERNAL(vector);

  attributes->is_maskable = true;
  attributes->can_enable = true;
  attributes->maybe_enable = true;
  attributes->can_disable = true;
  attributes->maybe_disable = true;
  attributes->can_raise = false;
  attributes->can_raise_on = false;
  attributes->cleared_by_acknowledge = true;
  attributes->can_get_affinity = false;
  attributes->can_set_affinity = false;
  attributes->can_clear = is_external;

  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_is_pending(
  rtems_vector_number vector,
  bool               *pending
)
{
  bsp_interrupt_assert(bsp_interrupt_is_valid_vector(vector));
  bsp_interrupt_assert(pending != NULL);

  if (MBV_INTERRUPT_VECTOR_IS_EXTERNAL(vector)) {
    uint32_t index = MBV_INTERRUPT_VECTOR_EXTERNAL_TO_INDEX(vector);

    *pending = (MBV_INTC->isr & (UINT32_C(1) << index)) != 0;
    return RTEMS_SUCCESSFUL;
  }

  if (vector == MBV_INTERRUPT_VECTOR_TIMER) {
    *pending = (read_csr(mip) & MIP_MTIP) != 0;
    return RTEMS_SUCCESSFUL;
  }

  _Assert(vector == MBV_INTERRUPT_VECTOR_SOFTWARE);
  *pending = (read_csr(mip) & MIP_MSIP) != 0;
  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_raise(rtems_vector_number vector)
{
  (void) vector;
  bsp_interrupt_assert(bsp_interrupt_is_valid_vector(vector));
  return RTEMS_UNSATISFIED;
}

rtems_status_code bsp_interrupt_clear(rtems_vector_number vector)
{
  bsp_interrupt_assert(bsp_interrupt_is_valid_vector(vector));

  if (MBV_INTERRUPT_VECTOR_IS_EXTERNAL(vector)) {
    uint32_t index = MBV_INTERRUPT_VECTOR_EXTERNAL_TO_INDEX(vector);

    MBV_INTC->iar = UINT32_C(1) << index;
    return RTEMS_SUCCESSFUL;
  }

  return RTEMS_UNSATISFIED;
}

rtems_status_code bsp_interrupt_vector_is_enabled(
  rtems_vector_number vector,
  bool               *enabled
)
{
  bsp_interrupt_assert(bsp_interrupt_is_valid_vector(vector));
  bsp_interrupt_assert(enabled != NULL);

  if (MBV_INTERRUPT_VECTOR_IS_EXTERNAL(vector)) {
    uint32_t index = MBV_INTERRUPT_VECTOR_EXTERNAL_TO_INDEX(vector);

    *enabled = (MBV_INTC->ier & (UINT32_C(1) << index)) != 0;
    return RTEMS_SUCCESSFUL;
  }

  if (vector == MBV_INTERRUPT_VECTOR_TIMER) {
    *enabled = (read_csr(mie) & MIP_MTIP) != 0;
    return RTEMS_SUCCESSFUL;
  }

  _Assert(vector == MBV_INTERRUPT_VECTOR_SOFTWARE);
  *enabled = (read_csr(mie) & MIP_MSIP) != 0;
  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_vector_enable(rtems_vector_number vector)
{
  bsp_interrupt_assert(bsp_interrupt_is_valid_vector(vector));

  if (MBV_INTERRUPT_VECTOR_IS_EXTERNAL(vector)) {
    uint32_t index = MBV_INTERRUPT_VECTOR_EXTERNAL_TO_INDEX(vector);
    rtems_interrupt_lock_context lock_context;

    rtems_interrupt_lock_acquire(&mbv_intc_lock, &lock_context);
    MBV_INTC->ier |= UINT32_C(1) << index;
    rtems_interrupt_lock_release(&mbv_intc_lock, &lock_context);
    return RTEMS_SUCCESSFUL;
  }

  if (vector == MBV_INTERRUPT_VECTOR_TIMER) {
    set_csr(mie, MIP_MTIP);
    return RTEMS_SUCCESSFUL;
  }

  _Assert(vector == MBV_INTERRUPT_VECTOR_SOFTWARE);
  set_csr(mie, MIP_MSIP);
  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_vector_disable(rtems_vector_number vector)
{
  bsp_interrupt_assert(bsp_interrupt_is_valid_vector(vector));

  if (MBV_INTERRUPT_VECTOR_IS_EXTERNAL(vector)) {
    uint32_t index = MBV_INTERRUPT_VECTOR_EXTERNAL_TO_INDEX(vector);
    rtems_interrupt_lock_context lock_context;

    rtems_interrupt_lock_acquire(&mbv_intc_lock, &lock_context);
    MBV_INTC->ier &= ~(UINT32_C(1) << index);
    rtems_interrupt_lock_release(&mbv_intc_lock, &lock_context);
    return RTEMS_SUCCESSFUL;
  }

  if (vector == MBV_INTERRUPT_VECTOR_TIMER) {
    clear_csr(mie, MIP_MTIP);
    return RTEMS_SUCCESSFUL;
  }

  _Assert(vector == MBV_INTERRUPT_VECTOR_SOFTWARE);
  clear_csr(mie, MIP_MSIP);
  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_interrupt_set_priority(
  rtems_vector_number vector,
  uint32_t priority
)
{
  (void) vector;
  (void) priority;
  bsp_interrupt_assert(bsp_interrupt_is_valid_vector(vector));
  return RTEMS_UNSATISFIED;
}

rtems_status_code bsp_interrupt_get_priority(
  rtems_vector_number vector,
  uint32_t *priority
)
{
  (void) vector;
  (void) priority;
  bsp_interrupt_assert(bsp_interrupt_is_valid_vector(vector));
  bsp_interrupt_assert(priority != NULL);
  return RTEMS_UNSATISFIED;
}
