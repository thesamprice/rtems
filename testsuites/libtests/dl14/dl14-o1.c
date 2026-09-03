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
 * A loaded module with thread local storage variables. The variables
 * cover the .tdata (initialised) and .tbss (zero) TLS sections, global
 * and static visibility, and an alignment above the word size.
 */

#include <stdint.h>
#include <stdio.h>

#include "dl14-o1.h"

__thread int dl14_tls_data = 0x12345678;
__thread int dl14_tls_bss;
__thread uint64_t dl14_tls_wide = 0x1122334455667788ull;
static __thread uint32_t dl14_tls_local[4];

int dl14_get_data(void)
{
  return dl14_tls_data;
}

void dl14_set_data(int value)
{
  dl14_tls_data = value;
}

int dl14_get_bss(void)
{
  return dl14_tls_bss;
}

void dl14_set_bss(int value)
{
  dl14_tls_bss = value;
}

uint64_t dl14_get_wide(void)
{
  return dl14_tls_wide;
}

void dl14_set_wide(uint64_t value)
{
  dl14_tls_wide = value;
}

uint32_t dl14_local_sum(void)
{
  size_t i;
  uint32_t sum = 0;
  for (i = 0; i < 4; ++i) {
    sum += dl14_tls_local[i];
  }
  return sum;
}

void dl14_local_set(int index, uint32_t value)
{
  dl14_tls_local[index & 3] = value;
}

/*
 * Check this thread sees the initial values of the module's TLS
 * variables. Returns 0 when every value is correct.
 */
int dl14_check_initial(void)
{
  int bad = 0;
  if (dl14_tls_data != 0x12345678) {
    printf("dl14: tls data initial value bad: 0x%08x\n", dl14_tls_data);
    ++bad;
  }
  if (dl14_tls_bss != 0) {
    printf("dl14: tls bss initial value bad: 0x%08x\n", dl14_tls_bss);
    ++bad;
  }
  if (dl14_tls_wide != 0x1122334455667788ull) {
    printf("dl14: tls wide initial value bad\n");
    ++bad;
  }
  if (dl14_local_sum() != 0) {
    printf("dl14: tls local initial value bad\n");
    ++bad;
  }
  return bad;
}
