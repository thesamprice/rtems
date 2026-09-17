/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVESP32
 *
 * @brief Pin ownership for the ESP32-C3.
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

/*
 * Two drivers configuring the same pad is not a theoretical problem here: it
 * has happened twice.  The I2C driver routes SDA and SCL to GPIO5 and GPIO6
 * while a GPIO test was using GPIO4 and GPIO5, and UART1 takes GPIO7 and
 * GPIO10, which a note listing the free pads still described as free.  In
 * both cases each driver wrote its own registers, the last writer won, and
 * nothing reported anything.
 *
 * The whole table is a bitmask plus one pointer per pad, and the claim is
 * taken under an interrupt lock rather than a mutex so that it is callable
 * from driver initialisation, before any thread that could block exists.
 */

#include <bsp/pin.h>

#include <rtems.h>

RTEMS_INTERRUPT_LOCK_DEFINE( static, esp32c3_pin_lock, "ESP32C3 pin" )

static uint32_t esp32c3_pin_claimed;

static const char *esp32c3_pin_owners[ BSP_PIN_COUNT ];

bool bsp_pin_claim( uint32_t pin, const char *owner )
{
  rtems_interrupt_lock_context lock_context;
  bool claimed;

  if ( pin >= BSP_PIN_COUNT ) {
    return false;
  }

  rtems_interrupt_lock_acquire( &esp32c3_pin_lock, &lock_context );

  if ( ( esp32c3_pin_claimed & ( 1U << pin ) ) != 0 ) {
    /*
     * Re-claiming a pad already held is how a driver that configures one in
     * several steps -- select the function, then set a direction -- avoids
     * having to remember whether it has claimed yet.  Only the same owner
     * may do it; a different one is the collision this exists to catch.
     */
    claimed = esp32c3_pin_owners[ pin ] == owner;
  } else {
    esp32c3_pin_claimed |= 1U << pin;
    esp32c3_pin_owners[ pin ] = owner;
    claimed = true;
  }

  rtems_interrupt_lock_release( &esp32c3_pin_lock, &lock_context );

  return claimed;
}

void bsp_pin_release( uint32_t pin )
{
  rtems_interrupt_lock_context lock_context;

  if ( pin >= BSP_PIN_COUNT ) {
    return;
  }

  rtems_interrupt_lock_acquire( &esp32c3_pin_lock, &lock_context );
  esp32c3_pin_claimed &= ~( 1U << pin );
  esp32c3_pin_owners[ pin ] = NULL;
  rtems_interrupt_lock_release( &esp32c3_pin_lock, &lock_context );
}

const char *bsp_pin_owner( uint32_t pin )
{
  rtems_interrupt_lock_context lock_context;
  const char *owner;

  if ( pin >= BSP_PIN_COUNT ) {
    return NULL;
  }

  rtems_interrupt_lock_acquire( &esp32c3_pin_lock, &lock_context );
  owner = ( esp32c3_pin_claimed & ( 1U << pin ) ) != 0 ?
    esp32c3_pin_owners[ pin ] : NULL;
  rtems_interrupt_lock_release( &esp32c3_pin_lock, &lock_context );

  return owner;
}
