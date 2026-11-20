/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup BspRiscvMbvValConsoleIrq
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
#include <dev/serial/uartlite_l.h>
#include <rtems/irq-extension.h>

#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <rtems/test.h>

#ifdef BSP_MICROBLAZE_FPGA_CONSOLE_INTERRUPTS

/**
 * @defgroup BspRiscvMbvValConsoleIrq spec:/bsp/riscv/mbv/val/console-irq
 *
 * @ingroup TestsuitesBspsValidationBsp0
 *
 * @brief Tests the interrupt-driven AXI UART Lite console of the MicroBlaze V
 *   BSP.
 *
 * This test case performs the following actions:
 *
 * - Look at the interrupt vector of the console device.  The console is open
 *   as standard output.
 *
 *   - Check that the console driver installed its handler and that the
 *     vector is enabled.
 *
 *   - Check that the device has its interrupt enabled.
 *
 * - Install a second handler on the vector which counts the interrupts, write
 *   a line to standard output and wait until it is transmitted.
 *
 *   - Check that the transmission raised interrupts.
 *
 * @{
 */

static volatile uint32_t interrupts;

static void Count( void *arg )
{
  (void) arg;
  ++interrupts;
}

static void IsConsoleHandler(
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

  if ( strcmp( info, "UART" ) == 0 ) {
    *(bool *) arg = true;
  }
}

/**
 * @brief Tests the interrupt-driven AXI UART Lite console of the MicroBlaze V
 *   BSP.
 */
T_TEST_CASE( BspRiscvMbvValConsoleIrq )
{
  static const char  line[] = "mbv: UART Lite interrupt-driven output\n";
  rtems_vector_number vector;
  rtems_status_code   sc;
  bool                found;
  bool                enabled;
  ssize_t             n;

  vector = MBV_INTERRUPT_VECTOR_EXTERNAL( mbv_cfg.uart_irq );

  found = false;
  sc = rtems_interrupt_handler_iterate( vector, IsConsoleHandler, &found );
  T_rsc_success( sc );
  T_true( found );

  sc = rtems_interrupt_vector_is_enabled( vector, &enabled );
  T_rsc_success( sc );
  T_true( enabled );

  T_ne_u32(
    XUartLite_GetStatusReg( mbv_cfg.uart_base ) & XUL_SR_INTR_ENABLED,
    0
  );

  interrupts = 0;
  sc = rtems_interrupt_handler_install(
    vector,
    "Count",
    RTEMS_INTERRUPT_SHARED,
    Count,
    NULL
  );
  T_rsc_success( sc );

  n = write( STDOUT_FILENO, line, sizeof( line ) - 1 );
  T_eq_ssz( n, (ssize_t) sizeof( line ) - 1 );
  T_eq_int( tcdrain( STDOUT_FILENO ), 0 );

  T_gt_u32( interrupts, 0 );

  sc = rtems_interrupt_handler_remove( vector, Count, NULL );
  T_rsc_success( sc );
}

/** @} */

#endif /* BSP_MICROBLAZE_FPGA_CONSOLE_INTERRUPTS */
