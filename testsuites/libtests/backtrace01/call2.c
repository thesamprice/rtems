/*
 * Copyright (c) 2026 Chris Johns. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software
 * is freely granted, provided that this notice is preserved.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unwind.h>

#include "tmacros.h"

#include "calls.h"

#define CALL_STACK_RECS 10
struct call_stack_recs {
  size_t index;
  uintptr_t calls[CALL_STACK_RECS];
};

static _Unwind_Reason_Code unwined_tracer(struct _Unwind_Context* ctx, void* arg) {
  struct call_stack_recs* recs = (struct call_stack_recs*) arg;
  uintptr_t ip =  _Unwind_GetIP(ctx);
  if (recs->index < CALL_STACK_RECS) {
    size_t i = recs->index++;
    recs->calls[i] = ip;
  }
  return _URC_NO_REASON;
}

#define SIZEOF(a, b) (((uintptr_t) b) - ((uintptr_t) a))

extern void Init(void);
extern void _Thread_Handler(void);

#define FUNC_END_DECL(name) extern char name##_end[]

FUNC_END_DECL(Init);
FUNC_END_DECL(call1);
FUNC_END_DECL(call2);

struct call_stack {
  uintptr_t call;
  size_t size;
  bool called;
  const char* sym;
};

static int call_stacl_compare(const void* a, const void* b) {
  const struct call_stack* csa = (struct call_stack*) a;
  const struct call_stack* csb = (struct call_stack*) b;
  if (csa->call == csb->call) {
    return 0;
  }
  return csa->call < csb->call ? -1 : 1;
}

int call2(int a1, int a2) {
  (void) a1;
  (void) a2;
  /*
   * Create at runtime to pick up the address map
   */
  struct call_stack calls[] = {
    { (uintptr_t) Init, FUNC_SIZE(Init), false, "Init" },
    { (uintptr_t) call1, FUNC_SIZE(call1), false, "call1" },
    { (uintptr_t) call2, FUNC_SIZE(call2), false, "call2" },
    { (uintptr_t) _Thread_Handler, 1024, false,  "_Thread_Handler" },
  };
  const size_t calls_size = (sizeof(calls) / sizeof(calls[0]));
  qsort(calls, calls_size, sizeof(struct call_stack), call_stacl_compare);
  struct call_stack_recs bt;
  memset(&bt, 0, sizeof(bt));
  _Unwind_Backtrace(unwined_tracer, &bt);
  printf("\nCalls: %zu\n", bt.index);
  for (size_t i = 0; i < bt.index; ++i) {
     size_t found = calls_size;
     for (size_t j = 0; j < calls_size; ++j) {
       if (bt.calls[i] >= calls[j].call  &&
           bt.calls[i] < (calls[j].call + calls[j].size)) {
         found = j;
       }
     }
     const char* sym = "not found";
     size_t off = 0;
     if (found < calls_size) {
       sym = calls[found].sym;
       off = bt.calls[i] - calls[found].call;
       calls[found].called = true;
     }
     printf(" %2zu %lx %s + %zu\n", i, bt.calls[i], sym, off);
  }
  for (size_t j = 0; j < calls_size; ++j) {
    rtems_test_assert(calls[j].called);
  }
  return 11;
}

END_LABEL(call2);
