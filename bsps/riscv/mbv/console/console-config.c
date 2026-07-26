/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVMBV
 *
 * @brief Console configuration for the AMD MicroBlaze V generic BSP.
 *
 * The console is the Xilinx AXI UART Lite.  The terminal device driver is
 * shared with the MicroBlaze BSPs (bsps/microblaze/shared/dev/serial).  It is
 * interrupt driven by default and polled if the
 * BSP_MICROBLAZE_FPGA_CONSOLE_INTERRUPTS BSP option is disabled.
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
#include <bsp/console-termios.h>
#include <bsp/irq.h>

#include <rtems/bspIo.h>

#include <dev/serial/uartlite.h>

static uart_lite_context mbv_uart_lite_instance = {
  .base = RTEMS_TERMIOS_DEVICE_CONTEXT_INITIALIZER("UARTLITE"),
  .address = MBV_UART_BASE,
  .initial_baud = BSP_CONSOLE_BAUD,
  .enabled = 1,
#ifdef BSP_MICROBLAZE_FPGA_CONSOLE_INTERRUPTS
  .irq = MBV_IRQ_UART_LITE
#endif
};

const console_device console_device_table[] = {
  {
    .device_file = "/dev/ttyS0",
    .probe = console_device_probe_default,
    .handler = &microblaze_uart_fns,
    .context = &mbv_uart_lite_instance.base
  }
};

const size_t console_device_count = RTEMS_ARRAY_SIZE(console_device_table);

static void mbv_output_char(char c)
{
  XUartLite_SendByte(MBV_UART_BASE, (u8) c);
}

static int mbv_poll_char(void)
{
  if (XUartLite_IsReceiveEmpty(MBV_UART_BASE)) {
    return -1;
  }

  return (int) (uint8_t) XUartLite_ReadReg(MBV_UART_BASE, XUL_RX_FIFO_OFFSET);
}

BSP_output_char_function_type BSP_output_char = mbv_output_char;

BSP_polling_getchar_function_type BSP_poll_char = mbv_poll_char;
