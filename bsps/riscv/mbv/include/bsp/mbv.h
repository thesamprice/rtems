/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVMBV
 *
 * @brief Device definitions for the AMD MicroBlaze V generic platform.
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

#ifndef LIBBSP_RISCV_MBV_MBV_H
#define LIBBSP_RISCV_MBV_MBV_H

#include <bsp.h>

/*
 * The AXI Interrupt Controller and AXI Timer register blocks are identical to
 * the ones used by the MicroBlaze BSPs, so the register definitions are
 * shared with bsps/microblaze.
 */
#include <bsp/intc.h>
#include <bsp/microblaze-timer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The AXI Interrupt Controller of the platform.
 *
 * Its interrupt output is wired to the Machine External Interrupt (MEIP) of
 * the hart.  There is no PLIC and no CLINT on this platform.
 */
#define MBV_INTC ((volatile Microblaze_INTC *) MBV_INTC_BASE)

/**
 * @brief The dual-channel AXI Timer of the platform.
 *
 * Channel 0 generates the clock tick via interrupt controller input
 * #MBV_TIMER_IRQ.  Channel 1 is used as a free-running counter for the
 * timecounter and the CPU counter.
 */
#define MBV_TIMER ((volatile Microblaze_Timer *) MBV_TIMER_BASE)

#ifdef __cplusplus
}
#endif

#endif /* LIBBSP_RISCV_MBV_MBV_H */
