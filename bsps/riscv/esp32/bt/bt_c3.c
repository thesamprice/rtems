/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVESP32BT
 *
 * @brief This source file contains the implementation of the ESP32-C3
 *   Bluetooth hardware bring-up.
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

#include <bsp.h>
#include <bsp/bt.h>
#include <bsp/irq.h>

#include <rtems/counter.h>

#define RTC_REG( off ) \
  ( *( (volatile uint32_t *) ( RTC_CNTL_BASE + (off) ) ) )
#define SYSCON_REG( off ) \
  ( *( (volatile uint32_t *) ( SYSCON_BASE + (off) ) ) )

/* soc/esp32c3/register/soc/rtc_cntl_reg.h */
#define RTC_CNTL_DIG_PWC     0x0088
#define BT_FORCE_PD          ( 1u << 11 )
#define BT_FORCE_PU          ( 1u << 12 )
#define BT_PD_EN             ( 1u << 27 )
#define RTC_CNTL_DIG_ISO     0x008c
#define BT_FORCE_ISO         ( 1u << 22 )
#define BT_FORCE_NOISO       ( 1u << 23 )

/* soc/esp32c3/register/soc/syscon_reg.h */
#define SYSTEM_WIFI_CLK_EN   0x0014
#define SYSTEM_WIFI_RST_EN   0x0018

/*
 * SYSTEM_BT_BASEBAND_EN and SYSTEM_BT_LC_EN.  ESP-IDF's own
 * SYSTEM_WIFI_CLK_BT_EN_M is defined as 0 on this chip, so
 * periph_module_enable( PERIPH_BT_MODULE ) enables nothing and something else
 * has to do the work -- the same trap the WiFi port hit with the MAC clock,
 * where SYSTEM_WIFI_CLK_WIFI_EN_M is likewise 0.  The comment on
 * RTEMS_WIFI_CLK_EXTRA in the WiFi glue records bits 11, 12, 16 and 17 as
 * still clear after WiFi bring-up and unnamed by any public header; three of
 * those four are these.
 */
#define BT_BASEBAND_CLK_EN   ( 1u << 11 )
#define BT_LC_CLK_EN         ( ( 1u << 16 ) | ( 1u << 17 ) )
#define BT_CLK_EN            ( BT_BASEBAND_CLK_EN | BT_LC_CLK_EN )

/*
 * SYSTEM_WIFI_CLK_WIFI_BT_COMMON_M.  Shared with WiFi, so it is only ever set
 * here, never cleared.
 */
#define WIFI_BT_COMMON_CLK   0x78078Fu

/*
 * The Bluetooth half of MODEM_RESET_FIELD_WHEN_PU: BTBB, BTMAC, RW_BTMAC,
 * RW_BTMAC_REG and BTBB_REG.  RW_BTLP and RW_BTLP_REG are the low power
 * module, which ESP-IDF leaves out of the power-up reset, so they are left out
 * here too.
 */
#define BT_RESET_WHEN_PU                                                      \
  ( ( 1u << 3 ) | ( 1u << 4 ) | ( 1u << 9 ) | ( 1u << 11 ) | ( 1u << 13 ) )

/*
 * ESP-IDF waits 10us after clearing the power-down force before it touches the
 * block.  rtems_task_wake_after() cannot express that: it rounds to a whole
 * tick and yields, and this runs with the domain half up.  The CPU counter is
 * running by the time any of this is reachable.
 */
static void bt_delay_us( uint32_t us )
{
  rtems_counter_ticks start = rtems_counter_read();
  rtems_counter_ticks want = rtems_counter_nanoseconds_to_ticks( us * 1000u );

  while ( rtems_counter_read() - start < want ) {
    /* wait */
  }
}

void bsp_esp32_bt_get_state( bsp_esp32_bt_state *state )
{
  state->dig_pwc = RTC_REG( RTC_CNTL_DIG_PWC );
  state->dig_iso = RTC_REG( RTC_CNTL_DIG_ISO );
  state->clk_en = SYSCON_REG( SYSTEM_WIFI_CLK_EN );
}

bool bsp_esp32_bt_is_enabled( void )
{
  bsp_esp32_bt_state state;

  bsp_esp32_bt_get_state( &state );

  return ( state.dig_pwc & BT_FORCE_PD ) == 0 &&
         ( state.dig_iso & BT_FORCE_ISO ) == 0 &&
         ( state.clk_en & BT_CLK_EN ) == BT_CLK_EN;
}

rtems_status_code bsp_esp32_bt_enable( void )
{
  uint32_t reg;

  /*
   * Powered, and forced so rather than left to the automatic control.
   *
   * ESP-IDF clears only BT_FORCE_PD, because it can rely on BT_FORCE_PU and
   * BT_FORCE_NOISO still holding their reset value of 1.  On this boot path
   * they do not.  The WiFi port measured DIG_PWC as 0x00000800 and DIG_ISO as
   * 0x00400080 by the time the port runs: bit 11 is BT_FORCE_PD and bit 22 is
   * BT_FORCE_ISO, so Bluetooth arrives forced down and isolated with every
   * FORCE_PU and FORCE_NOISO bit clear.  Clearing the negatives alone returns
   * the domain to automatic control, which is the state it is already in and
   * which leaves the block dark.
   *
   * BT_PD_EN is cleared so nothing power-gates the domain behind our back.
   */
  reg = RTC_REG( RTC_CNTL_DIG_PWC );
  reg &= ~( BT_FORCE_PD | BT_PD_EN );
  reg |= BT_FORCE_PU;
  RTC_REG( RTC_CNTL_DIG_PWC ) = reg;

  bt_delay_us( 10u );

  /* The common clock has to be on across the reset pulse. */
  reg = SYSCON_REG( SYSTEM_WIFI_CLK_EN );
  SYSCON_REG( SYSTEM_WIFI_CLK_EN ) = reg | WIFI_BT_COMMON_CLK;

  /* Reset the Bluetooth block now it has power: assert, then release. */
  reg = SYSCON_REG( SYSTEM_WIFI_RST_EN );
  SYSCON_REG( SYSTEM_WIFI_RST_EN ) = reg | BT_RESET_WHEN_PU;
  SYSCON_REG( SYSTEM_WIFI_RST_EN ) = reg & ~BT_RESET_WHEN_PU;

  /*
   * Out of isolation.  Order matters: power, then reset, then this.
   * BT_FORCE_NOISO for the same reason as BT_FORCE_PU above.
   */
  reg = RTC_REG( RTC_CNTL_DIG_ISO );
  reg &= ~BT_FORCE_ISO;
  reg |= BT_FORCE_NOISO;
  RTC_REG( RTC_CNTL_DIG_ISO ) = reg;

  /*
   * The baseband and link controller clocks, which unlike the common ones stay
   * on.  A controller reads its own registers before anything else, and
   * without these the window is dark rather than merely idle.
   */
  reg = SYSCON_REG( SYSTEM_WIFI_CLK_EN );
  SYSCON_REG( SYSTEM_WIFI_CLK_EN ) = reg | BT_CLK_EN;

  /*
   * Read back rather than assume.  Every bring-up failure seen on this part so
   * far has been a write that did not stick, and reporting it here is the
   * difference between a diagnosable error and a controller that hangs on its
   * first register read.
   */
  if ( !bsp_esp32_bt_is_enabled() ) {
    return RTEMS_IO_ERROR;
  }

  return RTEMS_SUCCESSFUL;
}

rtems_status_code bsp_esp32_bt_disable( void )
{
  uint32_t reg;

  /*
   * Clocks first, so the block is stopped before it is isolated rather than
   * losing its supply mid-access.  Only the Bluetooth bits: the common ones
   * are WiFi's as well.
   */
  reg = SYSCON_REG( SYSTEM_WIFI_CLK_EN );
  SYSCON_REG( SYSTEM_WIFI_CLK_EN ) = reg & ~BT_CLK_EN;

  reg = RTC_REG( RTC_CNTL_DIG_ISO );
  reg &= ~BT_FORCE_NOISO;
  reg |= BT_FORCE_ISO;
  RTC_REG( RTC_CNTL_DIG_ISO ) = reg;

  reg = RTC_REG( RTC_CNTL_DIG_PWC );
  reg &= ~BT_FORCE_PU;
  reg |= BT_FORCE_PD;
  RTC_REG( RTC_CNTL_DIG_PWC ) = reg;

  return RTEMS_SUCCESSFUL;
}
