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
#include <bsp/mbv.h>

#include <rtems.h>
#include <rtems/sysinit.h>

#include <string.h>
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

#ifdef MBV_USE_FDT

/*
 * The compatible strings of the peripherals.  These are the ones the Xilinx
 * device tree generator emits and the ones the classic MicroBlaze BSP looks
 * for, since the platform uses the same IP.
 */
#define MBV_FDT_INTC_COMPATIBLE "xlnx,xps-intc-1.00.a"
#define MBV_FDT_TIMER_COMPATIBLE "xlnx,xps-timer-1.00.a"
#define MBV_FDT_UARTLITE_COMPATIBLE "xlnx,xps-uartlite-1.00.a"
#define MBV_FDT_UART16550_COMPATIBLE "xlnx,xps-uart16550-2.00.a"

/**
 * @brief Finds the node of the index-th enabled device with this compatible
 *   string.
 *
 * Nodes with a status other than "okay" or "ok" are skipped, so that a design
 * which describes a peripheral it does not populate does not shadow the one it
 * does.  An absent status property means the device is enabled.
 *
 * @return The node offset, or a negative libfdt error code.
 */
static int mbv_fdt_find( const void *fdt, const char *compatible, int index )
{
  int node = -1;

  for ( ;; ) {
    const char *status;
    int         len;

    node = fdt_node_offset_by_compatible( fdt, node, compatible );

    if ( node < 0 ) {
      return node;
    }

    status = fdt_getprop( fdt, node, "status", &len );

    if (
      status != NULL && len > 0 &&
      strcmp( status, "okay" ) != 0 && strcmp( status, "ok" ) != 0
    ) {
      continue;
    }

    if ( index == 0 ) {
      return node;
    }

    --index;
  }
}

/**
 * @brief Returns the first address of the reg property of the node.
 *
 * The number of cells which make up the address is a property of the parent
 * bus, so it has to be asked for rather than assumed.  A design with a 64-bit
 * address map is described with two address cells even if this BSP is built
 * for RV32, and an address which does not fit in a pointer is rejected instead
 * of being truncated.
 *
 * @return The address, or @a fallback if the node has no usable reg property.
 */
static uintptr_t mbv_fdt_reg( const void *fdt, int node, uintptr_t fallback )
{
  const void *val;
  int         parent;
  int         ac;
  int         len;
  uint64_t    addr;

  parent = fdt_parent_offset( fdt, node );

  if ( parent < 0 ) {
    return fallback;
  }

  ac = fdt_address_cells( fdt, parent );

  if ( ac != 1 && ac != 2 ) {
    return fallback;
  }

  val = fdt_getprop( fdt, node, "reg", &len );

  if ( val == NULL || len < (int) ( (unsigned int) ac * sizeof( fdt32_t ) ) ) {
    return fallback;
  }

  if ( ac == 1 ) {
    addr = fdt32_ld( (const fdt32_t *) val );
  } else {
    addr = fdt64_ld( (const fdt64_t *) val );
  }

#if UINTPTR_MAX < UINT64_MAX
  if ( addr > UINTPTR_MAX ) {
    return fallback;
  }
#endif

  return (uintptr_t) addr;
}

/**
 * @brief Returns the first cell of a property of the node.
 *
 * @return The value, or @a fallback if the node has no such property.
 */
static uint32_t mbv_fdt_u32(
  const void *fdt,
  int         node,
  const char *name,
  uint32_t    fallback
)
{
  const void *val;
  int         len;

  val = fdt_getprop( fdt, node, name, &len );

  if ( val == NULL || len < (int) sizeof( fdt32_t ) ) {
    return fallback;
  }

  return fdt32_ld( (const fdt32_t *) val );
}

/*
 * The interrupt parent of every peripheral is the AXI Interrupt Controller,
 * which uses two interrupt cells.  The first is the controller input, the
 * second the kind of interrupt, which the BSP takes from the kind-of-intr
 * property of the controller itself instead.
 */
#define mbv_fdt_irq( fdt, node, fallback ) \
  mbv_fdt_u32( fdt, node, "interrupts", fallback )

static void mbv_fdt_configure( const void *fdt )
{
  int node;

  node = mbv_fdt_find( fdt, MBV_FDT_INTC_COMPATIBLE, 0 );

  if ( node >= 0 ) {
    mbv_cfg.intc = MBV_DEVICE(
      Microblaze_INTC,
      mbv_fdt_reg( fdt, node, (uintptr_t) mbv_cfg.intc )
    );
    mbv_cfg.intc_kind_of_edge = mbv_fdt_u32(
      fdt,
      node,
      "xlnx,kind-of-intr",
      mbv_cfg.intc_kind_of_edge
    );
  }

  /*
   * The clock tick and the free running counter of the timecounter are the two
   * channels of the first AXI Timer, the software raised interrupt is channel
   * 0 of the second one.  Which is which is a decision of this BSP and not
   * something the device tree can express, so the timers are taken in the
   * order in which the tree lists them.
   */
  node = mbv_fdt_find( fdt, MBV_FDT_TIMER_COMPATIBLE, 0 );

  if ( node >= 0 ) {
    mbv_cfg.timer = MBV_DEVICE(
      Microblaze_Timer,
      mbv_fdt_reg( fdt, node, (uintptr_t) mbv_cfg.timer )
    );
    mbv_cfg.timer_frequency = mbv_fdt_u32(
      fdt,
      node,
      "clock-frequency",
      mbv_cfg.timer_frequency
    );
    mbv_cfg.timer_irq = mbv_fdt_irq( fdt, node, mbv_cfg.timer_irq );
  }

  node = mbv_fdt_find( fdt, MBV_FDT_TIMER_COMPATIBLE, 1 );

  if ( node >= 0 ) {
    mbv_cfg.timer_2 = MBV_DEVICE(
      Microblaze_Timer,
      mbv_fdt_reg( fdt, node, (uintptr_t) mbv_cfg.timer_2 )
    );
    mbv_cfg.timer_2_irq = mbv_fdt_irq( fdt, node, mbv_cfg.timer_2_irq );
  }

  node = mbv_fdt_find( fdt, MBV_FDT_UARTLITE_COMPATIBLE, 0 );

  if ( node >= 0 ) {
    mbv_cfg.uart_base = mbv_fdt_reg( fdt, node, mbv_cfg.uart_base );
    mbv_cfg.uart_irq = mbv_fdt_irq( fdt, node, mbv_cfg.uart_irq );
  }

  /*
   * The reg property of an AXI UART 16550 node is the base address of the IP,
   * while the 16550 register block sits at an offset into it which the
   * reg-offset property gives.  The Xilinx device tree generator emits 0x1000
   * and so does the QEMU machine, which is also the value the classic
   * MicroBlaze BSPs assume, so use it when the property is absent.
   */
  node = mbv_fdt_find( fdt, MBV_FDT_UART16550_COMPATIBLE, 0 );

  if ( node >= 0 ) {
    uintptr_t base = mbv_fdt_reg( fdt, node, 0 );

    if ( base != 0 ) {
      mbv_cfg.uart_16550_base =
        base + mbv_fdt_u32( fdt, node, "reg-offset", 0x1000 );
    }

    mbv_cfg.uart_16550_irq = mbv_fdt_irq( fdt, node, mbv_cfg.uart_16550_irq );
  }
}

#endif /* MBV_USE_FDT */

#if MBV_FDT_PROBE_ADDRESS != 0
/**
 * @brief Looks for a device tree blob at a fixed address.
 *
 * A platform without a boot loader has no way to hand a device tree pointer to
 * the program, so the only thing left is to agree on an address.  The QEMU
 * amd-microblaze-v-generic machine is such a platform: it synthesizes no
 * device tree, has no mask ROM to run before the program, and leaves a1 zero,
 * but a blob can be loaded anywhere in its address space with
 *
 *   -device loader,file=<blob>,addr=<address>,force-raw=on
 *
 * The address is validated, and an address at which there is no device tree is
 * not an error: probing is a guess, and a guess which does not pan out has to
 * leave the BSP with its build time configuration rather than stop it.  Note
 * that the address does have to be readable, since there is nothing else that
 * could tell the difference between unmapped memory and memory holding no
 * device tree.
 */
static void mbv_fdt_probe( void )
{
  /*
   * A device tree from a boot loader wins.  The start code has already copied
   * it into the blob by the time this runs, which is what makes the pointer
   * differ from the empty tree.
   */
  if ( mbv_fdt != &mbv_fdt_empty_tree[ 0 ] ) {
    return;
  }

  bsp_fdt_copy( (const void *) (uintptr_t) MBV_FDT_PROBE_ADDRESS );
}
#endif /* MBV_FDT_PROBE_ADDRESS */

#if defined( MBV_USE_FDT ) || MBV_FDT_PROBE_ADDRESS != 0
/*
 * Find and parse the device tree before the interrupt controller is
 * initialized by bsp_start() at RTEMS_SYSINIT_BSP_START and before the free
 * running counter is started at RTEMS_SYSINIT_CPU_COUNTER.
 * RTEMS_SYSINIT_BSP_EARLY also precedes RTEMS_SYSINIT_ZERO_MEMORY and
 * RTEMS_SYSINIT_WORKSPACE, so a device tree which happens to live inside the
 * RTEMS memory region has already been copied out of it by then.
 */
static void mbv_fdt_initialize( void )
{
#if MBV_FDT_PROBE_ADDRESS != 0
  mbv_fdt_probe();
#endif
#ifdef MBV_USE_FDT
  mbv_fdt_configure( mbv_fdt );
#endif
}

RTEMS_SYSINIT_ITEM(
  mbv_fdt_initialize,
  RTEMS_SYSINIT_BSP_EARLY,
  RTEMS_SYSINIT_ORDER_FIRST
);
#endif
