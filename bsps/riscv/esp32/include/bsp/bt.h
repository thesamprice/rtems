/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVESP32
 *
 * @brief ESP32 Bluetooth hardware bring-up interface.
 */

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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef LIBBSP_ESP32_BSP_BT_H
#define LIBBSP_ESP32_BSP_BT_H

#include <rtems.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup RTEMSBSPsRISCVESP32BT ESP32 Bluetooth
 *
 * @ingroup RTEMSBSPsRISCVESP32
 *
 * @brief This group contains the Bluetooth hardware bring-up for the ESP32-C3.
 *
 * This is the half of Bluetooth support that is the BSP's: the power domain,
 * the clocks, the resets and the interrupt sources.  It brings the Bluetooth
 * block from the state the ROM leaves it in -- powered down and isolated -- to
 * one where a link layer controller can be started on it, and it makes the
 * seven Bluetooth interrupt sources reachable through the ordinary RTEMS
 * interrupt API.
 *
 * It does not contain a Bluetooth stack and there is no radio traffic here.
 * The link layer on this part is a binary controller, libble_app.a from the
 * esp32c3-bt-lib repository, which is out of the BSP's scope for the same
 * reason the WiFi libraries are: it needs an operating system adapter with
 * some ninety entry points, and that belongs in a glue layer above the BSP.
 * What that controller needs from below is what is here.
 *
 * Interrupts are installed with rtems_interrupt_handler_install() on the
 * vectors the chip header names: #BT_MAC_INTR, #BT_BB_INTR, #BT_BB_NMI_INTR,
 * #RWBT_INTR, #RWBLE_INTR, #RWBT_NMI_INTR and #RWBLE_NMI_INTR.  No Bluetooth
 * specific interrupt call is needed once those are in the matrix table.
 *
 * @{
 */

/**
 * @brief This structure provides the hardware state this driver controls.
 *
 * The raw register values rather than decoded flags, so that a caller
 * diagnosing a bring-up failure sees what the hardware actually says.  The
 * bring-up is a sequence of read-modify-writes against three registers and
 * every failure mode observed so far shows up as one of them not holding the
 * value that was written.
 */
typedef struct {
  /**
   * @brief This member contains RTC_CNTL_DIG_PWC_REG.
   *
   * Bit 11 forces the Bluetooth domain down, bit 12 forces it up and bit 27
   * hands it to the automatic power gate.
   */
  uint32_t dig_pwc;

  /**
   * @brief This member contains RTC_CNTL_DIG_ISO_REG.
   *
   * Bit 22 isolates the Bluetooth domain and bit 23 forces it out of
   * isolation.
   */
  uint32_t dig_iso;

  /**
   * @brief This member contains SYSTEM_WIFI_CLK_EN_REG.
   *
   * Bit 11 is the Bluetooth baseband clock and bits 16 and 17 are the link
   * controller's.  The bits shared with WiFi are in the low half.
   */
  uint32_t clk_en;
} bsp_esp32_bt_state;

/**
 * @brief Brings the Bluetooth block out of power-down and reset.
 *
 * Powers the Bluetooth domain, takes it out of isolation, enables the baseband
 * and link controller clocks and pulses the Bluetooth resets.  Safe to call
 * when WiFi is already up: only the Bluetooth bits are written, and the clock
 * bits the two share are set, never cleared.  A second call re-runs the
 * sequence, which resets the block, so a caller that has already started a
 * controller should not make one.
 *
 * @retval RTEMS_SUCCESSFUL Successful operation.
 * @retval RTEMS_IO_ERROR The hardware did not hold what was written.  This
 *   means the domain did not come up; bsp_esp32_bt_get_state() says which
 *   register disagreed.
 */
rtems_status_code bsp_esp32_bt_enable( void );

/**
 * @brief Returns the Bluetooth block to power-down and isolation.
 *
 * Clears the Bluetooth clocks and forces the domain down and isolated.  The
 * clock bits shared with WiFi are left alone, so this does not disturb a
 * running WiFi.
 *
 * @retval RTEMS_SUCCESSFUL Successful operation.
 */
rtems_status_code bsp_esp32_bt_disable( void );

/**
 * @brief Reports whether the Bluetooth block is powered, out of isolation and
 *   clocked.
 *
 * @return Returns true, if all three are the case, otherwise false.
 */
bool bsp_esp32_bt_is_enabled( void );

/**
 * @brief Reports the three registers the bring-up drives.
 *
 * @param[out] state is the pointer to a bsp_esp32_bt_state object.  The
 *   registers are read in the order of the structure's members.
 */
void bsp_esp32_bt_get_state( bsp_esp32_bt_state *state );

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LIBBSP_ESP32_BSP_BT_H */
