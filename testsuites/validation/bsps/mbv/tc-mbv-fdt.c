/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup BspRiscvMbvValFdt
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
#include <bsp/fdt.h>
#include <bsp/irq.h>
#include <bsp/mbv.h>

#include <libfdt.h>

#include <rtems/test.h>

/**
 * @defgroup BspRiscvMbvValFdt spec:/bsp/riscv/mbv/val/fdt
 *
 * @ingroup TestsuitesBspsValidationBsp0
 *
 * @brief Tests the device tree support of the MicroBlaze V BSP.
 *
 * This test case performs the following actions:
 *
 * - Get the device tree of the BSP.
 *
 *   - Check that it is a valid device tree, also if no device tree was handed
 *     to the BSP.
 *
 * - Map interrupt specifiers of the AXI Interrupt Controller.
 *
 *   - Check that the first interrupt cell selects the interrupt controller
 *     input and that the second one does not matter.
 *
 * @{
 */

/**
 * @brief Tests the device tree support of the MicroBlaze V BSP.
 */
T_TEST_CASE( BspRiscvMbvValFdt )
{
  const void *fdt;
  uint32_t    intr[ 2 ];

  fdt = bsp_fdt_get();
  T_assert_not_null( fdt );
  T_eq_int( fdt_check_header( fdt ), 0 );

  intr[ 0 ] = 3;
  intr[ 1 ] = 0;
  T_eq_u32( bsp_fdt_map_intr( intr, 2 ), MBV_INTERRUPT_VECTOR_EXTERNAL( 3 ) );

  intr[ 1 ] = 2;
  T_eq_u32( bsp_fdt_map_intr( intr, 2 ), MBV_INTERRUPT_VECTOR_EXTERNAL( 3 ) );
}

/** @} */
