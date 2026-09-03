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

/*
 * Thread local storage in a dynamically loaded module.
 *
 * The module dl14-o1.o defines TLS variables in .tdata and .tbss. The
 * test checks:
 *
 *  - a thread that existed before the load sees the initial values,
 *  - a thread created after the load sees the initial values,
 *  - writes in one thread are not visible in any other thread,
 *  - the initial values are intact after an unload and reload.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>

#include <dlfcn.h>

#include <rtems.h>

#include "dl-load.h"

#include "tmacros.h"

typedef int (*get_int_t)(void);
typedef void (*set_int_t)(int);
typedef uint64_t (*get_u64_t)(void);
typedef void (*set_u64_t)(uint64_t);
typedef uint32_t (*get_u32_t)(void);
typedef void (*set_u32_2_t)(int, uint32_t);

static struct {
  get_int_t   get_data;
  set_int_t   set_data;
  get_int_t   get_bss;
  set_int_t   set_bss;
  get_u64_t   get_wide;
  set_u64_t   set_wide;
  get_u32_t   local_sum;
  set_u32_2_t local_set;
  get_int_t   check_initial;
} mod;

#define WORKER_EVENT RTEMS_EVENT_1
#define WORKER_DONE  RTEMS_EVENT_2

static rtems_id init_task;
static volatile int worker_status;

static void* dl_load_obj(const char* name)
{
  void* handle;

  printf("load: %s\n", name);

  handle = dlopen(name, RTLD_NOW | RTLD_GLOBAL);
  if (!handle) {
    printf("dlopen failed: %s\n", dlerror());
    return NULL;
  }

  printf("handle: %p loaded\n", handle);
  return handle;
}

static bool dl_load_syms(void* handle)
{
  mod.get_data = dlsym(handle, "dl14_get_data");
  mod.set_data = dlsym(handle, "dl14_set_data");
  mod.get_bss = dlsym(handle, "dl14_get_bss");
  mod.set_bss = dlsym(handle, "dl14_set_bss");
  mod.get_wide = dlsym(handle, "dl14_get_wide");
  mod.set_wide = dlsym(handle, "dl14_set_wide");
  mod.local_sum = dlsym(handle, "dl14_local_sum");
  mod.local_set = dlsym(handle, "dl14_local_set");
  mod.check_initial = dlsym(handle, "dl14_check_initial");
  if (mod.get_data == NULL || mod.set_data == NULL || mod.get_bss == NULL ||
      mod.set_bss == NULL || mod.get_wide == NULL || mod.set_wide == NULL ||
      mod.local_sum == NULL || mod.local_set == NULL ||
      mod.check_initial == NULL) {
    printf("dlsym failed: symbol not found\n");
    return false;
  }
  return true;
}

/*
 * Exercise the module's TLS variables in the calling thread: check the
 * initial values, write thread unique values and check they read back.
 */
static int dl_tls_exercise(uint32_t key)
{
  int bad = mod.check_initial();

  if (bad != 0) {
    return bad;
  }

  mod.set_data((int)key);
  mod.set_bss((int)~key);
  mod.set_wide(((uint64_t)key << 32) | key);
  mod.local_set(0, key);
  mod.local_set(3, key + 1);

  if (mod.get_data() != (int)key || mod.get_bss() != (int)~key ||
      mod.get_wide() != (((uint64_t)key << 32) | key) ||
      mod.local_sum() != (key + key + 1)) {
    printf("dl14: tls write/read back failed in thread 0x%08x\n",
           (unsigned int)key);
    return 1;
  }

  return 0;
}

static void worker_entry(rtems_task_argument arg)
{
  rtems_event_set out;

  /*
   * Wait until the module is loaded, exercise the module's TLS in this
   * thread and hand the result back.
   */
  (void)rtems_event_receive(WORKER_EVENT, RTEMS_WAIT | RTEMS_EVENT_ALL,
                            RTEMS_NO_TIMEOUT, &out);

  worker_status = dl_tls_exercise((uint32_t)arg);

  (void)rtems_event_send(init_task, WORKER_DONE);

  (void)rtems_task_suspend(RTEMS_SELF);
}

static int dl_run_worker(rtems_id worker, const char* which)
{
  rtems_event_set out;
  rtems_status_code sc;

  worker_status = -1;

  sc = rtems_event_send(worker, WORKER_EVENT);
  if (sc != RTEMS_SUCCESSFUL) {
    printf("worker event send failed\n");
    return 1;
  }

  sc = rtems_event_receive(WORKER_DONE, RTEMS_WAIT | RTEMS_EVENT_ALL,
                           rtems_clock_get_ticks_per_second() * 5, &out);
  if (sc != RTEMS_SUCCESSFUL) {
    printf("worker done receive failed\n");
    return 1;
  }

  if (worker_status != 0) {
    printf("worker %s tls checks failed\n", which);
    return 1;
  }

  printf("worker %s tls checks pass\n", which);
  return 0;
}

static rtems_id dl_start_worker(rtems_name name, uint32_t key)
{
  rtems_id id;
  rtems_status_code sc;

  sc = rtems_task_create(name, 2, RTEMS_MINIMUM_STACK_SIZE * 2,
                         RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES, &id);
  if (sc != RTEMS_SUCCESSFUL) {
    printf("worker task create failed: %s\n", rtems_status_text(sc));
    return 0;
  }

  sc = rtems_task_start(id, worker_entry, (rtems_task_argument)key);
  if (sc != RTEMS_SUCCESSFUL) {
    printf("worker task start failed\n");
    return 0;
  }

  return id;
}

int dl_load_test(void)
{
  void* handle;
  rtems_id worker_before;
  rtems_id worker_after;

  init_task = rtems_task_self();

  /*
   * This worker exists before the module is loaded; the loader installs
   * the module's TLS image into its TLS area at load time.
   */
  worker_before = dl_start_worker(rtems_build_name('W', 'K', 'R', 'B'),
                                  0xb0000001);
  if (worker_before == 0) {
    return 1;
  }

  /* Let the worker run to its event receive. */
  rtems_task_wake_after(2);

  handle = dl_load_obj("/dl14-o1.o");
  if (handle == NULL) {
    return 1;
  }
  if (!dl_load_syms(handle)) {
    return 1;
  }

  /*
   * The loading thread sees the initial values and thread unique
   * writes.
   */
  if (dl_tls_exercise(0xa0000001) != 0) {
    printf("loader thread tls checks failed\n");
    return 1;
  }
  printf("loader thread tls checks pass\n");

  if (dl_run_worker(worker_before, "created before load") != 0) {
    return 1;
  }

  /*
   * This worker is created after the load; the thread create extension
   * installs the module's TLS image into its TLS area.
   */
  worker_after = dl_start_worker(rtems_build_name('W', 'K', 'R', 'A'),
                                 0xc0000001);
  if (worker_after == 0) {
    return 1;
  }

  if (dl_run_worker(worker_after, "created after load") != 0) {
    return 1;
  }

  /*
   * The loader thread's values must not have been changed by the
   * workers.
   */
  if (mod.get_data() != (int)0xa0000001 ||
      mod.get_bss() != (int)~0xa0000001 ||
      mod.get_wide() != ((0xa0000001ull << 32) | 0xa0000001ull)) {
    printf("loader thread tls values changed by other threads\n");
    return 1;
  }
  printf("loader thread tls isolation pass\n");

  (void)rtems_task_delete(worker_before);
  (void)rtems_task_delete(worker_after);

  if (dlclose(handle) < 0) {
    printf("dlclose failed: %s\n", dlerror());
    return 1;
  }

  /*
   * Reload; the TLS block is reallocated and the initial values must be
   * intact.
   */
  handle = dl_load_obj("/dl14-o1.o");
  if (handle == NULL) {
    return 1;
  }
  if (!dl_load_syms(handle)) {
    return 1;
  }
  if (mod.check_initial() != 0) {
    printf("reload tls initial values failed\n");
    return 1;
  }
  printf("reload tls initial values pass\n");

  if (dlclose(handle) < 0) {
    printf("dlclose failed: %s\n", dlerror());
    return 1;
  }

  return 0;
}
