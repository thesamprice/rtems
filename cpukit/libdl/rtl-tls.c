/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup rtems_rtld
 *
 * @brief RTEMS Run-Time Link Editor Thread Local Storage
 *
 * TLS support the RTL.
 */

/*
 *  COPYRIGHT (c) 2023 Chris Johns <chrisj@rtems.org>
 *
 * Copyright (C) 2023 embedded brains GmbH & Co. KG
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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "rtl-tls.h"

#include <rtems/rtl/rtl-allocator.h>
#include <rtems/rtl/rtl-obj.h>
#include <rtems/rtl/rtl-trace.h>
#include <rtems/rtl/rtl.h>
#include "rtl-bit-alloc.h"
#include "rtl-error.h"

#include <rtems/score/cpuimpl.h>
#include <rtems/score/percpu.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/tls.h>
#include <rtems/score/userextimpl.h>

void* rtems_rtl_tls_get_base(void) {
  return _CPU_Get_TLS_thread_pointer(&_Thread_Get_executing()->Registers);
}

/**
 * A loaded object's TLS block. The offset is relative to the
 * architecture's TLS thread pointer, the image is the initialisation
 * image for the block and is installed into every thread's TLS area.
 */
typedef struct rtems_rtl_tls_region {
  rtems_chain_node node;  /**< Region chain node. */
  size_t offset;          /**< Thread pointer relative offset. */
  size_t size;            /**< Size of the block in bytes. */
  size_t balloc_offset;   /**< Bit allocator offset of the allocation. */
  size_t balloc_size;     /**< Bit allocator size of the allocation. */
  void* image;            /**< Initialisation image, size bytes. */
} rtems_rtl_tls_region;

/**
 * The TLS support state. The regions chain holds a region per loaded
 * object with TLS variables and the bit allocator manages the extra
 * TLS space CONFIGURE_MAXIMUM_THREAD_LOCAL_STORAGE_SIZE reserves in
 * every thread beyond the base image's allocation. The state is
 * protected by the RTL lock.
 */
static struct {
  rtems_rtl_bit_alloc* alloc;    /**< Extra TLS space allocator. */
  size_t extra_begin;            /**< Thread pointer relative offset of
                                  *   the extra TLS space. */
  size_t extra_size;             /**< Size of the extra TLS space. */
  rtems_chain_control regions;   /**< The loaded regions. */
  User_extensions_Control ext;   /**< Thread create extension. */
  bool ext_active;               /**< The extension is registered. */
  bool open;                     /**< The state is initialised. */
} rtl_tls;

/**
 * The size of the extra TLS space in every thread's TLS area and its
 * thread pointer relative offset. The extra space is the difference
 * between the configured maximum TLS size and the size the base image
 * needs, which _TLS_Get_allocation_size computes; the calculation of
 * the needed size is mirrored here. Offsets are only valid for TLS
 * variants where the thread pointer addresses the start of the TLS
 * data and offsets grow upwards (variant I).
 */
static size_t rtems_rtl_tls_extra(size_t* begin) {
#if defined(CPU_THREAD_LOCAL_STORAGE_VARIANT) && \
    (CPU_THREAD_LOCAL_STORAGE_VARIANT == 10)
  const volatile TLS_Configuration* config = &_TLS_Configuration;
  const uintptr_t stack_align = CPU_STACK_ALIGNMENT;
  uintptr_t size = (uintptr_t)config->size;
  uintptr_t tls_align;
  uintptr_t needed;
  uintptr_t allocated;

  if (_Thread_Maximum_TLS_size == 0) {
    return 0;
  }

  tls_align = RTEMS_ALIGN_UP((uintptr_t)config->alignment, stack_align);

  needed = RTEMS_ALIGN_UP(sizeof(TLS_Dynamic_thread_vector), stack_align);
  needed += RTEMS_ALIGN_UP(sizeof(TLS_Thread_control_block), stack_align);
  needed += RTEMS_ALIGN_UP(size, stack_align);
  if (tls_align > stack_align) {
    needed += tls_align - stack_align;
  }

  allocated = _TLS_Get_allocation_size();
  if (allocated <= needed) {
    return 0;
  }

  if (begin != NULL) {
    *begin = RTEMS_ALIGN_UP(size, stack_align);
  }

  return allocated - needed;
#else
  /*
   * Only TLS variant I with thread pointer relative offsets starting
   * at the TLS data (RISC-V and similar) is supported.
   */
  (void)begin;
  return 0;
#endif
}

size_t rtems_rtl_tls_avail(void) {
  return rtems_rtl_tls_extra(NULL);
}

/**
 * Install every registered region's initialisation image into a
 * thread's TLS area. Called for existing threads when a region is
 * registered and from the thread create extension for new threads.
 */
static void rtems_rtl_tls_thread_install(Thread_Control* thread) {
  char* base = _CPU_Get_TLS_thread_pointer(&thread->Registers);
  rtems_chain_node* node = rtems_chain_first(&rtl_tls.regions);

  while (!rtems_chain_is_tail(&rtl_tls.regions, node)) {
    rtems_rtl_tls_region* region = (rtems_rtl_tls_region*)node;
    memcpy(base + region->offset, region->image, region->size);
    node = rtems_chain_next(node);
  }
}

static bool rtems_rtl_tls_thread_visitor(Thread_Control* thread, void* arg) {
  (void)arg;
  rtems_rtl_tls_thread_install(thread);
  return false;
}

/*
 * The thread begin extension runs in the new thread just before its
 * entry point, when the thread pointer register is live; at thread
 * create time the thread's context, and with it the thread pointer,
 * is not initialised yet.
 */
static void rtems_rtl_tls_thread_begin(Thread_Control* executing) {
  /*
   * The RTL lock is a recursive mutex so a load that creates threads,
   * for example from a constructor, does not deadlock.
   */
  rtems_rtl_lock();
  rtems_rtl_tls_thread_install(executing);
  rtems_rtl_unlock();
}

static bool rtems_rtl_tls_open(void) {
  size_t begin = 0;
  size_t extra;

  if (rtl_tls.open) {
    return true;
  }

  extra = rtems_rtl_tls_extra(&begin);
  if (extra == 0) {
    rtems_rtl_set_error(
        ENOMEM, "no TLS space for loaded objects, configure "
                "CONFIGURE_MAXIMUM_THREAD_LOCAL_STORAGE_SIZE");
    return false;
  }

  /*
   * The allocator manages thread pointer relative offsets, not real
   * memory, so the base is the offset the extra space begins at.
   */
  rtl_tls.alloc = rtems_rtl_bit_alloc_open((void*)(uintptr_t)begin, extra,
                                           sizeof(uint32_t), 0);
  if (rtl_tls.alloc == NULL) {
    rtems_rtl_set_error(ENOMEM, "no memory for TLS allocator");
    return false;
  }

  rtl_tls.extra_begin = begin;
  rtl_tls.extra_size = extra;
  rtems_chain_initialize_empty(&rtl_tls.regions);
  rtl_tls.open = true;

  if (rtems_rtl_trace(RTEMS_RTL_TRACE_LOAD)) {
    printf("rtl: tls: extra space: offset=%zu size=%zu\n", begin, extra);
  }

  return true;
}

bool rtems_rtl_tls_module_alloc(rtems_rtl_obj* obj, size_t size,
                                size_t alignment) {
  rtems_rtl_tls_region* region;
  size_t balloc_size;
  void* balloc;
  size_t offset;

  if (!rtems_rtl_tls_open()) {
    return false;
  }

  if (alignment < sizeof(uint32_t)) {
    alignment = sizeof(uint32_t);
  }

  /*
   * The bit allocator works in 32bit blocks. Over allocate by the
   * alignment so the block can be aligned.
   */
  balloc_size = size + alignment - sizeof(uint32_t);
  balloc = rtems_rtl_bit_alloc_balloc(rtl_tls.alloc, balloc_size);
  if (balloc == NULL) {
    rtems_rtl_set_error(ENOMEM, "no TLS space left for object");
    return false;
  }

  region = rtems_rtl_alloc_new(RTEMS_RTL_ALLOC_OBJECT, sizeof(*region), true);
  if (region == NULL) {
    rtems_rtl_bit_alloc_bfree(rtl_tls.alloc, balloc, balloc_size);
    rtems_rtl_set_error(ENOMEM, "no memory for TLS region");
    return false;
  }

  offset = (size_t)(uintptr_t)balloc;

  rtems_chain_set_off_chain(&region->node);
  region->offset = RTEMS_ALIGN_UP(offset, alignment);
  region->size = size;
  region->balloc_offset = offset;
  region->balloc_size = balloc_size;
  region->image = NULL;

  obj->tls_offset = region->offset;
  obj->tls_size = size;
  obj->tls_region = region;

  if (rtems_rtl_trace(RTEMS_RTL_TRACE_LOAD)) {
    printf("rtl: tls: %s: block: offset=%zu size=%zu align=%zu\n",
           rtems_rtl_obj_oname(obj), region->offset, size, alignment);
  }

  return true;
}

bool rtems_rtl_tls_module_register(rtems_rtl_obj* obj) {
  rtems_rtl_tls_region* region = obj->tls_region;

  if (region == NULL || obj->tls_image == NULL) {
    rtems_rtl_set_error(EINVAL, "no TLS block or image to register");
    return false;
  }

  region->image = obj->tls_image;

  /*
   * Install the image into threads that begin from now on, then into
   * every thread that already exists. A thread that begins between
   * the two steps is installed twice which is harmless.
   */
  if (!rtl_tls.ext_active) {
    memset(&rtl_tls.ext, 0, sizeof(rtl_tls.ext));
    rtl_tls.ext.Callouts.thread_begin = rtems_rtl_tls_thread_begin;
    _User_extensions_Add_set(&rtl_tls.ext);
    rtl_tls.ext_active = true;
  }

  rtems_chain_append(&rtl_tls.regions, &region->node);

  _Thread_Iterate(rtems_rtl_tls_thread_visitor, NULL);

  return true;
}

void rtems_rtl_tls_module_free(rtems_rtl_obj* obj) {
  rtems_rtl_tls_region* region = obj->tls_region;

  if (region != NULL) {
    if (!rtems_chain_is_node_off_chain(&region->node)) {
      rtems_chain_extract(&region->node);
    }
    rtems_rtl_bit_alloc_bfree(rtl_tls.alloc,
                              (void*)(uintptr_t)region->balloc_offset,
                              region->balloc_size);
    rtems_rtl_alloc_del(RTEMS_RTL_ALLOC_OBJECT, region);
  }

  if (obj->tls_image != NULL) {
    rtems_rtl_alloc_del(RTEMS_RTL_ALLOC_OBJECT, obj->tls_image);
  }

  obj->tls_region = NULL;
  obj->tls_image = NULL;
  obj->tls_offset = 0;
  obj->tls_size = 0;
}
