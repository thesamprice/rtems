/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVMBV
 *
 * @brief CPU counter for the AMD MicroBlaze V generic BSP.
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

#include <bsp.h>
#include <bsp/mbv.h>

#include <rtems/sysinit.h>
#include <rtems/score/cpu.h>

uint32_t _CPU_Counter_frequency( void )
{
  return mbv_cfg.timer_frequency;
}

CPU_Counter_ticks _CPU_Counter_read( void )
{
  return (CPU_Counter_ticks) MBV_TIMER->tcr1;
}

static void mbv_counter_initialize( void )
{
  volatile Microblaze_Timer *timer = MBV_TIMER;

  /*
   * Channel 1: free-running up counter used by the CPU counter.  With a load
   * value of zero and auto reload, the counter wraps through the full 32-bit
   * range.  No interrupt is generated.
   *
   * This runs from a system initialization item so that the CPU counter
   * works in applications which do not need the clock driver.
   */
  timer->tcsr1 = MICROBLAZE_TIMER_TCSR0_T0INT;
  timer->tlr1 = 0;
  timer->tcsr1 = MICROBLAZE_TIMER_TCSR0_LOAD0;
  timer->tcsr1 = MICROBLAZE_TIMER_TCSR0_ARHT0 | MICROBLAZE_TIMER_TCSR0_ENT0;
}

RTEMS_SYSINIT_ITEM(
  mbv_counter_initialize,
  RTEMS_SYSINIT_CPU_COUNTER,
  RTEMS_SYSINIT_ORDER_FIRST
);
