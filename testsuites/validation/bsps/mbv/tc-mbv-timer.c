/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup BspRiscvMbvValTimer
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
#include <rtems/counter.h>
#include <rtems/irq-extension.h>

#include <string.h>

#include <rtems/test.h>

/**
 * @defgroup BspRiscvMbvValTimer spec:/bsp/riscv/mbv/val/timer
 *
 * @ingroup TestsuitesBspsValidationBsp0
 *
 * @brief Tests the AXI Timer clock driver and CPU counter of the MicroBlaze V
 *   BSP.
 *
 * This test case performs the following actions:
 *
 * - Get the CPU counter frequency and read the CPU counter twice.
 *
 *   - Check that the frequency is the one of the device configuration.
 *
 *   - Check that the counter advances.
 *
 * - Look at the interrupt vector of the clock tick.
 *
 *   - Check that the clock driver installed its handler and that the vector
 *     is enabled.
 *
 * - Measure several clock ticks with the CPU counter and the uptime.
 *
 *   - Check that both measurements agree with the configured clock tick
 *     interval within ten percent.
 *
 * @{
 */

#define TICKS 5

static void IsClockHandler(
  void                   *arg,
  const char             *info,
  rtems_option            option,
  rtems_interrupt_handler handler,
  void                   *handler_arg
)
{
  (void) option;
  (void) handler;
  (void) handler_arg;

  if ( strcmp( info, "Clock" ) == 0 ) {
    *(bool *) arg = true;
  }
}

static rtems_interval WaitForNextTick( void )
{
  rtems_interval now;
  rtems_interval next;

  now = rtems_clock_get_ticks_since_boot();

  do {
    next = rtems_clock_get_ticks_since_boot();
  } while ( next == now );

  return next;
}

static void CheckWithinTenPercent( uint64_t actual, uint64_t expected )
{
  T_ge_u64( actual * 10, expected * 9 );
  T_le_u64( actual * 10, expected * 11 );
}

/**
 * @brief Tests the AXI Timer clock driver and CPU counter of the MicroBlaze V
 *   BSP.
 */
T_TEST_CASE( BspRiscvMbvValTimer )
{
  rtems_vector_number vector;
  rtems_status_code   sc;
  rtems_counter_ticks c0;
  rtems_counter_ticks c1;
  rtems_interval      t0;
  rtems_interval      t1;
  uint64_t            u0;
  uint64_t            u1;
  uint64_t            us_per_tick;
  bool                found;
  bool                enabled;

  T_eq_u32( rtems_counter_frequency(), mbv_cfg.timer_frequency );

  c0 = rtems_counter_read();
  c1 = rtems_counter_read();
  T_ne_u32( c1, c0 );

  vector = MBV_INTERRUPT_VECTOR_EXTERNAL( mbv_cfg.timer_irq );

  found = false;
  sc = rtems_interrupt_handler_iterate( vector, IsClockHandler, &found );
  T_rsc_success( sc );
  T_true( found );

  sc = rtems_interrupt_vector_is_enabled( vector, &enabled );
  T_rsc_success( sc );
  T_true( enabled );

  us_per_tick = rtems_configuration_get_microseconds_per_tick();

  t0 = WaitForNextTick();
  c0 = rtems_counter_read();
  u0 = rtems_clock_get_uptime_nanoseconds();

  do {
    t1 = rtems_clock_get_ticks_since_boot();
  } while ( t1 - t0 < TICKS );

  c1 = rtems_counter_read();
  u1 = rtems_clock_get_uptime_nanoseconds();

  CheckWithinTenPercent(
    rtems_counter_ticks_to_nanoseconds( rtems_counter_difference( c1, c0 ) ),
    TICKS * us_per_tick * 1000
  );
  CheckWithinTenPercent( u1 - u0, TICKS * us_per_tick * 1000 );
}

/** @} */
