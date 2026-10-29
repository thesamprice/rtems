/*
 * Copyright (C) 2026 Kinsey Moore <wkmoore@gmail.com>
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

#include <bsp/bootcard.h>
#include <bsp/fatal.h>
#include <bsp/irq-generic.h>
#include <bsp/linker-symbols.h>

#include <stdint.h>
#include <bsp/start.h>
#include <bsp/utility.h>

#define RTC_CNTL_WDTWPROTECT_REG (uintptr_t) 0xa8
#define RTC_CNTL_WDTWPROTECT_KEY 0x50d83aa1U

#define RTC_CNTL_WDTCONFIG0_REG (uintptr_t) 0x90
#define RTC_CNTL_WDTCONFIG0_EN  BSP_BIT32( 31 )

/*
 * "Enable WDT in flash boot", default 1, and independent of the enable bit
 * above: the watchdog still bites with it set and RTC_CNTL_WDTCONFIG0_EN
 * clear.  ESP-IDF's second-stage bootloader clears it, which is why an
 * ESP-IDF application never sees it and a direct-boot image always does.
 */
#define RTC_CNTL_WDT_FLASHBOOT_MOD_EN BSP_BIT32( 12 )

#define RTC_CNTL_SWD_CONF_REG          (uintptr_t) 0xac
#define RTC_CNTL_SWD_CONF_AUTO_FEED_EN BSP_BIT32( 31 )

#define RTC_CNTL_SWD_WPROTECT_REG (uintptr_t) 0xb0
#define RTC_CNTL_SWD_WPROTECT_KEY 0x8f1d312aU

#define RTC_CNTL_SW_CPU_STALL_REG (uintptr_t) 0xb4

#define RTC_READ( reg )         READ_REG( RTC_CNTL_BASE, reg )
#define RTC_WRITE( reg, value ) WRITE_REG( RTC_CNTL_BASE, reg, value )

#define TIMG_WDTCONFIG0_REG (uintptr_t) 0x48
#define TIMG_WDTCONFIG0_EN  BSP_BIT32( 31 )

/*
 * Timergroup 0's watchdog has its own write protection, with the same key as
 * the RTC one but a separate register.  Without unlocking it the write below
 * is silently dropped.
 */
#define TIMG_WDTWPROTECT_REG (uintptr_t) 0x64
#define TIMG_WDTWPROTECT_KEY 0x50d83aa1U

/* The same flash-boot mode as the RTC watchdog has, at a different bit. */
#define TIMG_WDT_FLASHBOOT_MOD_EN BSP_BIT32( 14 )

#define TIMG_READ( reg )         READ_REG( TIMG_BASE, reg )
#define TIMG_WRITE( reg, value ) WRITE_REG( TIMG_BASE, reg, value )

#define READ_REG( base, reg ) *(volatile uint32_t *) ( base + reg )
#define WRITE_REG( base, reg, value ) \
  *(volatile uint32_t *) ( base + reg ) = value

void bsp_start( void )
{
  uint32_t wdt_config0;
  uint32_t swd_conf;
  uint32_t timg_wdtconfig0;

  /* disable RTC watchdog write protection */
  RTC_WRITE( RTC_CNTL_WDTWPROTECT_REG, RTC_CNTL_WDTWPROTECT_KEY );

  /*
   * Disable the RTC watchdog.
   *
   * & ~EN, not & EN.  The original cleared every bit *except* the enable,
   * which is the inverse of what it reads as -- it left the watchdog running
   * and cleared its stage-action fields instead.
   *
   * That was accidentally safe rather than wrong in effect: RTC_CNTL_WDT_STG0
   * and its three siblings occupy bits 30:28 downwards and encode 0 as "no
   * action", so a watchdog with every stage set to nothing does nothing.  It
   * is corrected because the next reader should not have to work that out, and
   * because a mistake in this register presents on real silicon as a boot loop
   * with no other symptom -- QEMU does not model these, so it has never been
   * exercised either way.
   */
  wdt_config0 = RTC_READ( RTC_CNTL_WDTCONFIG0_REG );
  RTC_WRITE(
    RTC_CNTL_WDTCONFIG0_REG,
    wdt_config0 & ~( RTC_CNTL_WDTCONFIG0_EN | RTC_CNTL_WDT_FLASHBOOT_MOD_EN )
  );

  /* disable super watchdog write protection */
  RTC_WRITE( RTC_CNTL_SWD_WPROTECT_REG, RTC_CNTL_SWD_WPROTECT_KEY );

  /* Set SWD auto feed */
  swd_conf = RTC_READ( RTC_CNTL_SWD_CONF_REG );
  RTC_WRITE(
    RTC_CNTL_SWD_CONF_REG,
    swd_conf | RTC_CNTL_SWD_CONF_AUTO_FEED_EN
  );

  /* clear SWD stall register */
  RTC_WRITE( RTC_CNTL_SW_CPU_STALL_REG, 0 );

  /*
   * Disable timergroup 0's watchdog, the same correction as above -- and
   * unlock it first, which the RTC watchdog above gets and this one did not.
   *
   * Every write to the timergroup watchdog registers is discarded while
   * TIMG_WDTWPROTECT_REG holds anything other than the key, with no error and
   * no other symptom, so the disable was a no-op and the watchdog stayed
   * enabled at its reset timeout.
   *
   * The flash-boot mode bit matters more than the enable.  It defaults to 1
   * and is independent: clearing only TIMG_WDTCONFIG0_EN leaves a watchdog
   * that still bites, which is what the register read back as on hardware --
   * 0x0004c000, enable clear and flash-boot mode set.
   *
   * On silicon that is roughly a second and a half, after which the chip
   * resets with rst:0x7 (TG0WDT_SYS_RST).  Anything that boots and finishes
   * inside that window looks perfectly healthy, which is why every test so far
   * has: it only bites once something runs for longer, and the first thing
   * that did was bringing up WiFi.  QEMU models neither watchdog, so this
   * could not have shown up before hardware.
   */
  TIMG_WRITE( TIMG_WDTWPROTECT_REG, TIMG_WDTWPROTECT_KEY );

  timg_wdtconfig0 = TIMG_READ( TIMG_WDTCONFIG0_REG );
  TIMG_WRITE(
    TIMG_WDTCONFIG0_REG,
    timg_wdtconfig0 & ~( TIMG_WDTCONFIG0_EN | TIMG_WDT_FLASHBOOT_MOD_EN )
  );

  bsp_interrupt_initialize();
}

/* src is the offset in flash */
/*
 * The distance from the instruction window to the data window onto the same
 * SRAM, defined in the BSP's linker script.  A LINKER_SYMBOL rather than a
 * constant here so that changing the memory map changes one place.
 */
LINKER_SYMBOL( esp32c_iram_to_dram_delta );

BSP_START_TEXT_SECTION static inline void copy_from_flash_offset(
  void       *dest,
  const void *src,
  size_t      n
)
{
  /* The RAM load sections are offset from 0x0, offset from mapped flash base */
  uintptr_t flash_base = 0x3c000000;

  uintptr_t flash_address = ( (uintptr_t) src );
  flash_address += flash_base;
  memcpy( dest, (void *) flash_address, n );
}

BSP_START_TEXT_SECTION void bsp_start_copy_sections( void )
{
  copy_from_flash_offset(
    bsp_section_data_begin,
    bsp_section_data_load_begin,
    (size_t) bsp_section_data_size
  );

  /*
   * .fast_text is linked in the instruction window so that calls into it
   * resolve to 0x4038xxxx, but it is *written* through the data window.  The
   * two address the same SRAM; stores through the data bus are the access the
   * part guarantees, and are how ESP-IDF loads its own IRAM.
   *
   * esp32c_iram_to_dram_delta comes from the linker script, so the two places
   * that know the window layout cannot drift apart.
   */
  copy_from_flash_offset(
    (char *) bsp_section_fast_text_begin + (intptr_t) esp32c_iram_to_dram_delta,
    bsp_section_fast_text_load_begin,
    (size_t) bsp_section_fast_text_size
  );

  copy_from_flash_offset(
    bsp_section_fast_data_begin,
    bsp_section_fast_data_load_begin,
    (size_t) bsp_section_fast_data_size
  );
}
