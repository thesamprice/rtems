/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Copyright (C) 2026 embedded brains GmbH & Co. KG
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <bsp/bootcard.h>
#include <bsp/fdt.h>

#include <libfdt.h>

/*
 * Shut the board down through a SiFive test finisher if the device tree
 * declares one.
 *
 * Without this the only thing bsp_reset() can do is spin, which leaves a
 * simulator running after the system has halted.  An automated test runner
 * then cannot tell a test which failed and shut down from one which hung, and
 * every test costs the runner's full timeout however quickly it finishes.
 *
 * Fall back to spinning when there is no such device, which is the case on
 * real hardware.
 */

#define SIFIVE_TEST_FINISHER_PASS 0x5555U

void bsp_reset( rtems_fatal_source source, rtems_fatal_code code )
{
  const void *fdt;
  int         node;

  (void) source;
  (void) code;

  fdt = bsp_fdt_get();
  node = fdt_node_offset_by_compatible( fdt, -1, "sifive,test0" );

  if ( node >= 0 ) {
    int            len;
    const uint32_t *reg;

    reg = fdt_getprop( fdt, node, "reg", &len );

    if ( reg != NULL && len >= (int) sizeof( *reg ) ) {
      volatile uint32_t *finisher;

      finisher = (volatile uint32_t *) (uintptr_t) fdt32_to_cpu( reg[ 0 ] );

      while ( true ) {
        *finisher = SIFIVE_TEST_FINISHER_PASS;
      }
    }
  }

  while ( true ) {
    /* Nothing to shut down, so halt here. */
  }
}
