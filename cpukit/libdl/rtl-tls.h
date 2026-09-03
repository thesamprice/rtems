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

#if !defined(_RTEMS_RTL_TLS_H_)
#define _RTEMS_RTL_TLS_H_

#include <rtems/rtl/rtl-obj-fwd.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void* rtems_rtl_tls_get_base(void);

/**
 * Is there thread local storage available for loaded objects? Space
 * has to be reserved in every thread's TLS area by building the
 * application with CONFIGURE_MAXIMUM_THREAD_LOCAL_STORAGE_SIZE larger
 * than the base image's TLS allocation.
 *
 * @retval size The number of bytes of TLS space available to loaded
 *              objects.
 */
size_t rtems_rtl_tls_avail(void);

/**
 * Allocate thread local storage for an object file from the space
 * reserved by CONFIGURE_MAXIMUM_THREAD_LOCAL_STORAGE_SIZE. The offset
 * is relative to the architecture's TLS thread pointer and is the
 * value TLS relocations use. The allocator is a bit allocator with a
 * 32bit word resolution.
 *
 * On success the object's tls_offset and tls_size are set.
 *
 * @param obj The object file the space is for.
 * @param size The number of bytes to allocate.
 * @param alignment The alignment of the block.
 * @retval true The space was allocated.
 * @retval false The allocation failed, the RTL error is set.
 */
bool rtems_rtl_tls_module_alloc(rtems_rtl_obj* obj, size_t size,
                                size_t alignment);

/**
 * Register the object's TLS initialisation image. The image is the
 * complete TLS block for the object, ie the .tdata content followed
 * by zeros for the .tbss content, and ownership of the memory passes
 * to the TLS support. The image is copied into the TLS area of every
 * existing thread and a thread begin extension installs it into
 * every thread that begins after this call.
 *
 * @param obj The object file the image is for.
 * @retval true The image is registered.
 * @retval false The registration failed, the RTL error is set.
 */
bool rtems_rtl_tls_module_register(rtems_rtl_obj* obj);

/**
 * Release an object file's TLS space and initialisation image.
 *
 * @param obj The object file being unloaded.
 */
void rtems_rtl_tls_module_free(rtems_rtl_obj* obj);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
