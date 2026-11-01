/*
 * Copyright (c) 2026 Chris Johns. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software
 * is freely granted, provided that this notice is preserved.
 */

#include <stdio.h>

#include "calls.h"

/*
 * Return an updated value to stop the compiler from chaining to the
 * next call.
 */
int call1(int a1, int a2) {
  return call2(a1, a2) + 32;
}

END_LABEL(call1);
