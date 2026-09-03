/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Copyright (C) 2026 Samuel Price <thesamprice@gmail.com>
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

#include "tmacros.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <rtems/imfs.h>
#include <rtems/rtl/rtl.h>

#include "dl-load.h"

const char rtems_test_name[] = "libdl (RTL) 14";

/* forward declarations to avoid warnings */
static rtems_task Init(rtems_task_argument argument);

#include "dl14-tar.h"

#define TARFILE_START dl14_tar
#define TARFILE_SIZE dl14_tar_size

static int test(void)
{
  int ret;
  ret = dl_load_test();
  if (ret) {
    rtems_test_exit(ret);
  }
  return 0;
}

static void Init(rtems_task_argument arg)
{
  (void)arg;

  int te;

  TEST_BEGIN();

  te = rtems_tarfs_load("/", (void*)TARFILE_START, (size_t)TARFILE_SIZE);
  if (te != 0) {
    printf("untar failed: %d\n", te);
    rtems_test_exit(1);
    exit(1);
  }

  test();

  TEST_END();

  rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 4

#define CONFIGURE_MAXIMUM_TASKS 4

#define CONFIGURE_MAXIMUM_SEMAPHORES 1

/*
 * Reserve thread local storage beyond the base image's needs for the
 * TLS variables of dynamically loaded modules. The value must be a
 * multiple of CPU_STACK_ALIGNMENT and is the size of the whole TLS
 * area in every thread.
 */
#define CONFIGURE_MAXIMUM_THREAD_LOCAL_STORAGE_SIZE 4096

/*
 * The TLS area is part of each task's storage allocation; reserve
 * space for the workers' stacks plus their enlarged TLS areas.
 */
#define CONFIGURE_EXTRA_TASK_STACKS   (4 * (RTEMS_MINIMUM_STACK_SIZE +         CONFIGURE_MAXIMUM_THREAD_LOCAL_STORAGE_SIZE))

#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT_TASK_STACK_SIZE \
  (CONFIGURE_MINIMUM_TASK_STACK_SIZE + (4U * 1024U))

#define CONFIGURE_INIT_TASK_ATTRIBUTES \
  (RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT)

#define CONFIGURE_INIT

#include <rtems/confdefs.h>
