/*
 * Copyright (c) 2026 Chris Johns. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software
 * is freely granted, provided that this notice is preserved.
 */

#ifndef CALLS_H
#define CALLS_H

/*
 * Hack to embed size data into the image to detect the function size.
 */
#pragma GCC optimize ("no-toplevel-reorder")

#define END_LABEL(name) \
    asm(".globl " #name "_end\n" \
        #name "_end:")

#define FUNC_SIZE(name) \
    ((size_t)((char *)name##_end - (char *)name))

int call1(int a1, int a2);
int call2(int a1, int a2);

#endif
