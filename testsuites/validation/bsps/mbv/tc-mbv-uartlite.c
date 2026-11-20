/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup BspRiscvMbvValUartLite
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
#include <bsp/mbv.h>
#include <dev/serial/uartlite_l.h>
#include <rtems/bspIo.h>

#include <rtems/test.h>

/**
 * @defgroup BspRiscvMbvValUartLite spec:/bsp/riscv/mbv/val/uartlite
 *
 * @ingroup TestsuitesBspsValidationBsp0
 *
 * @brief Tests the polled AXI UART Lite console of the MicroBlaze V BSP.
 *
 * This test case performs the following actions:
 *
 * - Read the status register of the console device.
 *
 *   - Check that no receive error is flagged, which a missing or misplaced
 *     device would most likely show.
 *
 * - Output a line with BSP_output_char() and wait for the transmit FIFO to
 *   drain.
 *
 *   - Check that the transmit FIFO drained.
 *
 * - Poll for a character while no input is pending.
 *
 *   - Check that BSP_poll_char() returns -1.
 *
 * @{
 */

static uint32_t GetStatus( void )
{
  return XUartLite_GetStatusReg( mbv_cfg.uart_base );
}

static bool WaitForTransmitEmpty( void )
{
  int i;

  for ( i = 0; i < 1000000; ++i ) {
    if ( ( GetStatus() & XUL_SR_TX_FIFO_EMPTY ) != 0 ) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Tests the polled AXI UART Lite console of the MicroBlaze V BSP.
 */
T_TEST_CASE( BspRiscvMbvValUartLite )
{
  const char *line = "mbv: UART Lite polled output\n";
  uint32_t    status;

  status = GetStatus();
  T_eq_u32(
    status &
      ( XUL_SR_PARITY_ERROR | XUL_SR_FRAMING_ERROR | XUL_SR_OVERRUN_ERROR ),
    0
  );

  while ( *line != '\0' ) {
    ( *BSP_output_char )( *line );
    ++line;
  }

  T_true( WaitForTransmitEmpty() );
  T_eq_u32( GetStatus() & XUL_SR_TX_FIFO_FULL, 0 );

  if ( ( GetStatus() & XUL_SR_RX_FIFO_VALID_DATA ) == 0 ) {
    T_eq_int( ( *BSP_poll_char )(), -1 );
  }
}

/** @} */
