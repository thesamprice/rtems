/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVMBV
 *
 * @brief Device tree blob support of the AMD MicroBlaze V generic BSP.
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
#include <bsp/fdt.h>
#include <bsp/irq.h>

#include <rtems.h>

#include <sys/param.h>

#include <libfdt.h>

/*
 * A flattened device tree is big endian and MicroBlaze V is little endian, so
 * the words of the empty tree below have to be byte swapped.  The
 * cpu_to_fdt32() of libfdt is an inline function and cannot be used in a
 * static initializer.
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MBV_FDT_BE32( x ) \
  ( ( ( (uint32_t) ( x ) & UINT32_C( 0x000000ff ) ) << 24 ) | \
    ( ( (uint32_t) ( x ) & UINT32_C( 0x0000ff00 ) ) << 8 ) | \
    ( ( (uint32_t) ( x ) & UINT32_C( 0x00ff0000 ) ) >> 8 ) | \
    ( ( (uint32_t) ( x ) & UINT32_C( 0xff000000 ) ) >> 24 ) )
#else
#define MBV_FDT_BE32( x ) ( (uint32_t) ( x ) )
#endif

/*
 * A well-formed flattened device tree which contains nothing but an empty root
 * node.  This is the same tree fdt_create_empty_tree() would build, only as a
 * constant, which avoids linking the sequential write and read-write parts of
 * libfdt for 72 bytes of data.
 *
 * It exists so that bsp_fdt_get() never returns something which fails
 * fdt_check_header().  Users of the device tree are entitled to assume the
 * BSP hands them a valid tree once BSP_FDT_IS_SUPPORTED is defined:
 * rtems_ofw_init() runs unconditionally at RTEMS_SYSINIT_BSP_PRE_DRIVERS and
 * calls rtems_fatal_error_occurred(RTEMS_NOT_CONFIGURED) if the header does
 * not check out.  Returning an empty tree turns "there is no device tree"
 * into an empty lookup result instead of a fatal error.
 *
 * The layout is
 *
 *   0..39   the version 17 header
 *   40..55  the memory reservation block, holding only its terminator
 *   56..71  the structure block: FDT_BEGIN_NODE, the empty node name padded
 *           to four bytes, FDT_END_NODE, FDT_END
 *   72      the empty strings block
 */
static const RTEMS_ALIGNED( 8 ) uint32_t mbv_fdt_empty_tree[ 18 ] = {
  MBV_FDT_BE32( FDT_MAGIC ),          /* magic */
  MBV_FDT_BE32( 72 ),                 /* totalsize */
  MBV_FDT_BE32( 56 ),                 /* off_dt_struct */
  MBV_FDT_BE32( 72 ),                 /* off_dt_strings */
  MBV_FDT_BE32( 40 ),                 /* off_mem_rsvmap */
  MBV_FDT_BE32( 17 ),                 /* version */
  MBV_FDT_BE32( 16 ),                 /* last_comp_version */
  MBV_FDT_BE32( 0 ),                  /* boot_cpuid_phys */
  MBV_FDT_BE32( 0 ),                  /* size_dt_strings */
  MBV_FDT_BE32( 16 ),                 /* size_dt_struct */
  MBV_FDT_BE32( 0 ),                  /* rsvmap terminator address, high */
  MBV_FDT_BE32( 0 ),                  /* rsvmap terminator address, low */
  MBV_FDT_BE32( 0 ),                  /* rsvmap terminator size, high */
  MBV_FDT_BE32( 0 ),                  /* rsvmap terminator size, low */
  MBV_FDT_BE32( FDT_BEGIN_NODE ),
  MBV_FDT_BE32( 0 ),                  /* the empty root node name */
  MBV_FDT_BE32( FDT_END_NODE ),
  MBV_FDT_BE32( FDT_END )
};

/*
 * The storage for a device tree handed over by a boot loader or found by the
 * BSP.  It is not initialized, so it lives in .bss and costs nothing in the
 * image.  The .bss is cleared by the start code before bsp_fdt_copy() may be
 * called from there.
 */
static RTEMS_ALIGNED( 8 ) uint32_t
mbv_fdt_blob[ BSP_FDT_BLOB_SIZE_MAX / sizeof( uint32_t ) ];

/*
 * The device tree in use.  This is a private variable and not bsp_fdt_get(),
 * because a test program may interpose on bsp_fdt_get() to hand a device tree
 * of its own to the operating system, and the BSP must not configure its
 * hardware from that.
 */
static const void *mbv_fdt = &mbv_fdt_empty_tree[ 0 ];

void bsp_fdt_copy( const void *src )
{
  const uint32_t *s;
  uint32_t       *d;
  size_t          size;
  size_t          n;
  size_t          i;

  /*
   * Validate before the size is taken from the header.  The source is a boot
   * loader register value or a probed address, so it may well be garbage, and
   * fdt_totalsize() of garbage is garbage.
   */
  if ( src == NULL || fdt_check_header( src ) != 0 ) {
    return;
  }

  size = fdt_totalsize( src );

  /*
   * Refuse a device tree which does not fit.  Copying a prefix of it would
   * produce a blob which no longer checks out, which is worse than keeping
   * the empty tree.
   */
  if ( size > sizeof( mbv_fdt_blob ) ) {
    return;
  }

  s = (const uint32_t *) src;
  d = &mbv_fdt_blob[ 0 ];

  if ( s != d ) {
    n = ( size + sizeof( *d ) - 1 ) / sizeof( *d );

    for ( i = 0; i < n; ++i ) {
      d[ i ] = s[ i ];
    }

    rtems_cache_flush_multiple_data_lines(
      d,
      roundup2( size, CPU_CACHE_LINE_BYTES )
    );
  }

  mbv_fdt = d;
}

const void *bsp_fdt_get( void )
{
  return mbv_fdt;
}

uint32_t bsp_fdt_map_intr( const uint32_t *intr, size_t icells )
{
  (void) icells;

  /*
   * The interrupt parent of this platform is the AXI Interrupt Controller,
   * whose first interrupt cell is the controller input.  The second cell is
   * the kind of interrupt, which the vector numbering does not encode.
   */
  return MBV_INTERRUPT_VECTOR_EXTERNAL( intr[ 0 ] );
}
