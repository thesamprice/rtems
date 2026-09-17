/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVESP32
 *
 * @brief ESP32-C3 GPIO driver.
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
 * A pad on this part is driven through two peripherals rather than one.
 * IO_MUX picks which of the pad's functions reaches the pin and holds the
 * pull resistors and the input buffer enable; the GPIO peripheral holds the
 * data, direction and interrupt registers.  Configuring a pin therefore
 * always touches both, and selecting the plain GPIO function means setting
 * IO_MUX's MCU_SEL to PIN_FUNC_GPIO *and* pointing the GPIO matrix output
 * for that pad at SIG_GPIO_OUT_IDX, which is what makes GPIO_OUT drive it.
 */

#include <bsp.h>
#include <bsp/gpio.h>
#include <bsp/irq.h>
#include <bsp/pin.h>

#include <rtems/bspIo.h>

#include <inttypes.h>

#define GPIO_REG( off ) \
  ( *( (volatile uint32_t *) ( GPIO_BASE + (off) ) ) )
#define IO_MUX_REG( off ) \
  ( *( (volatile uint32_t *) ( IO_MUX_BASE + (off) ) ) )

#define GPIO_OUT          0x0004
#define GPIO_OUT_W1TS     0x0008
#define GPIO_OUT_W1TC     0x000c
#define GPIO_ENABLE       0x0020
#define GPIO_ENABLE_W1TS  0x0024
#define GPIO_ENABLE_W1TC  0x0028
#define GPIO_IN           0x003c
#define GPIO_STATUS       0x0044
#define GPIO_STATUS_W1TC  0x004c
#define GPIO_PIN( n )     ( 0x0074 + 4 * (n) )
#define GPIO_FUNC_OUT_SEL_CFG( n ) ( 0x0554 + 4 * (n) )

#define GPIO_PIN_INT_TYPE_SHIFT  7
#define GPIO_PIN_INT_TYPE_MASK   ( 0x7U << GPIO_PIN_INT_TYPE_SHIFT )
#define GPIO_PIN_INT_ENA_SHIFT   13
#define GPIO_PIN_INT_ENA_MASK    ( 0x1fU << GPIO_PIN_INT_ENA_SHIFT )
#define GPIO_PIN_INT_ENA_CPU     ( 0x1U << GPIO_PIN_INT_ENA_SHIFT )

/* INT_TYPE encoding, from the chip's technical reference manual. */
#define GPIO_INT_TYPE_DISABLE     0
#define GPIO_INT_TYPE_POSEDGE     1
#define GPIO_INT_TYPE_NEGEDGE     2
#define GPIO_INT_TYPE_ANYEDGE     3
#define GPIO_INT_TYPE_LOW_LEVEL   4
#define GPIO_INT_TYPE_HIGH_LEVEL  5

/* IO_MUX pad configuration register for GPIOn, and its fields. */
#define IO_MUX_PIN( n )   ( 0x0004 + 4 * (n) )
#define IO_MUX_FUN_PD     ( 1U << 7 )
#define IO_MUX_FUN_PU     ( 1U << 8 )
#define IO_MUX_FUN_IE     ( 1U << 9 )
#define IO_MUX_MCU_SEL_SHIFT 12
#define IO_MUX_MCU_SEL_MASK  ( 0x7U << IO_MUX_MCU_SEL_SHIFT )

/* IO_MUX function number that routes a pad to the GPIO peripheral, and the
 * GPIO matrix signal that means "driven by GPIO_OUT". */
#define IO_MUX_FUNC_GPIO  1
#define SIG_GPIO_OUT_IDX  128

static const char esp32c3_gpio_owner[] = "esp32c3 gpio";

static bool esp32c3_gpio_select_gpio_function( uint32_t pin )
{
  uint32_t mux;

  /* The shared layer's rtems_gpio_request_pin() already stops two GPIO users
   * colliding.  What it cannot see is a pad taken by I2C or UART1, which
   * never go through it -- so the claim is taken here as well, and this is
   * the only place that catches a cross-driver collision.
   *
   * It is never given back.  rtems_gpio_release_pin() does its bookkeeping
   * in the shared layer and there is no rtems_gpio_bsp_release() for it to
   * call, so a pad the GPIO driver has configured stays claimed for the life
   * of the application.  Re-requesting it as GPIO still works, because a
   * re-claim by the same owner succeeds; what is refused is I2C or UART1
   * taking a pad that GPIO used earlier and has since released.  That is a
   * false refusal, and it is the safe direction to be wrong in -- the other
   * way round is the silent collision this exists to stop. */
  if ( !bsp_pin_claim( pin, esp32c3_gpio_owner ) ) {
    printk(
      "esp32c3 gpio: GPIO%" PRIu32 " belongs to %s\n",
      pin,
      bsp_pin_owner( pin )
    );
    return false;
  }

  mux = IO_MUX_REG( IO_MUX_PIN( pin ) );

  mux &= ~IO_MUX_MCU_SEL_MASK;
  mux |= (uint32_t) IO_MUX_FUNC_GPIO << IO_MUX_MCU_SEL_SHIFT;

  /* Leave the input buffer on for outputs too.  It costs nothing and it is
   * what lets a driver read back the level it is driving, which is the only
   * way an output can be tested without external wiring. */
  mux |= IO_MUX_FUN_IE;

  IO_MUX_REG( IO_MUX_PIN( pin ) ) = mux;
  GPIO_REG( GPIO_FUNC_OUT_SEL_CFG( pin ) ) = SIG_GPIO_OUT_IDX;

  return true;
}

rtems_status_code rtems_gpio_bsp_multi_set(
  RTEMS_UNUSED uint32_t bank,
  uint32_t bitmask
)
{
  GPIO_REG( GPIO_OUT_W1TS ) = bitmask;

  return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_gpio_bsp_multi_clear(
  RTEMS_UNUSED uint32_t bank,
  uint32_t bitmask
)
{
  GPIO_REG( GPIO_OUT_W1TC ) = bitmask;

  return RTEMS_SUCCESSFUL;
}

uint32_t rtems_gpio_bsp_multi_read(
  RTEMS_UNUSED uint32_t bank,
  uint32_t bitmask
)
{
  return GPIO_REG( GPIO_IN ) & bitmask;
}

rtems_status_code rtems_gpio_bsp_multi_select(
  RTEMS_UNUSED rtems_gpio_multiple_pin_select *pins,
  RTEMS_UNUSED uint32_t pin_count,
  RTEMS_UNUSED uint32_t select_bank
)
{
  return RTEMS_NOT_DEFINED;
}

rtems_status_code rtems_gpio_bsp_specific_group_operation(
  RTEMS_UNUSED uint32_t bank,
  RTEMS_UNUSED uint32_t *pins,
  RTEMS_UNUSED uint32_t pin_count,
  RTEMS_UNUSED void *arg
)
{
  return RTEMS_NOT_DEFINED;
}

rtems_status_code rtems_gpio_bsp_set( RTEMS_UNUSED uint32_t bank, uint32_t pin )
{
  GPIO_REG( GPIO_OUT_W1TS ) = 1U << pin;

  return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_gpio_bsp_clear( RTEMS_UNUSED uint32_t bank, uint32_t pin )
{
  GPIO_REG( GPIO_OUT_W1TC ) = 1U << pin;

  return RTEMS_SUCCESSFUL;
}

uint32_t rtems_gpio_bsp_get_value( RTEMS_UNUSED uint32_t bank, uint32_t pin )
{
  return GPIO_REG( GPIO_IN ) & ( 1U << pin );
}

rtems_status_code rtems_gpio_bsp_select_input(
  RTEMS_UNUSED uint32_t bank,
  uint32_t pin,
  RTEMS_UNUSED void *bsp_specific
)
{
  if ( !esp32c3_gpio_select_gpio_function( pin ) ) {
    return RTEMS_RESOURCE_IN_USE;
  }

  GPIO_REG( GPIO_ENABLE_W1TC ) = 1U << pin;

  return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_gpio_bsp_select_output(
  RTEMS_UNUSED uint32_t bank,
  uint32_t pin,
  RTEMS_UNUSED void *bsp_specific
)
{
  if ( !esp32c3_gpio_select_gpio_function( pin ) ) {
    return RTEMS_RESOURCE_IN_USE;
  }

  GPIO_REG( GPIO_ENABLE_W1TS ) = 1U << pin;

  return RTEMS_SUCCESSFUL;
}

/*
 * The BSP specific function is an IO_MUX function number, so this is how a
 * pad is handed to UART, SPI or any other peripheral that can reach it.
 */
rtems_status_code rtems_gpio_bsp_select_specific_io(
  RTEMS_UNUSED uint32_t bank,
  uint32_t pin,
  uint32_t function,
  RTEMS_UNUSED void *pin_data
)
{
  uint32_t mux;

  if ( function > ( IO_MUX_MCU_SEL_MASK >> IO_MUX_MCU_SEL_SHIFT ) ) {
    return RTEMS_UNSATISFIED;
  }

  mux = IO_MUX_REG( IO_MUX_PIN( pin ) );
  mux &= ~IO_MUX_MCU_SEL_MASK;
  mux |= function << IO_MUX_MCU_SEL_SHIFT;
  IO_MUX_REG( IO_MUX_PIN( pin ) ) = mux;

  return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_gpio_bsp_set_resistor_mode(
  RTEMS_UNUSED uint32_t bank,
  uint32_t pin,
  rtems_gpio_pull_mode mode
)
{
  uint32_t mux = IO_MUX_REG( IO_MUX_PIN( pin ) );

  mux &= ~( IO_MUX_FUN_PU | IO_MUX_FUN_PD );

  switch ( mode ) {
    case PULL_UP:
      mux |= IO_MUX_FUN_PU;
      break;
    case PULL_DOWN:
      mux |= IO_MUX_FUN_PD;
      break;
    case NO_PULL_RESISTOR:
      break;
    default:
      return RTEMS_UNSATISFIED;
  }

  IO_MUX_REG( IO_MUX_PIN( pin ) ) = mux;

  return RTEMS_SUCCESSFUL;
}

/*
 * All 22 pads share one interrupt, so the whole status register is the event
 * line.  Clearing it here rather than leaving it to the caller matches what
 * the shared layer expects and is required for the edge-triggered types: an
 * unacknowledged status bit keeps the interrupt asserted.
 */
uint32_t rtems_gpio_bsp_interrupt_line( RTEMS_UNUSED rtems_vector_number vector )
{
  uint32_t event = GPIO_REG( GPIO_STATUS );

  GPIO_REG( GPIO_STATUS_W1TC ) = event;

  return event;
}

rtems_vector_number rtems_gpio_bsp_get_vector( RTEMS_UNUSED uint32_t bank )
{
  return GPIO_PROCPU_INTR;
}

rtems_status_code rtems_gpio_bsp_enable_interrupt(
  RTEMS_UNUSED uint32_t bank,
  uint32_t pin,
  rtems_gpio_interrupt interrupt
)
{
  uint32_t type;
  uint32_t reg;

  switch ( interrupt ) {
    case FALLING_EDGE:
      type = GPIO_INT_TYPE_NEGEDGE;
      break;
    case RISING_EDGE:
      type = GPIO_INT_TYPE_POSEDGE;
      break;
    case BOTH_EDGES:
      type = GPIO_INT_TYPE_ANYEDGE;
      break;
    case LOW_LEVEL:
      type = GPIO_INT_TYPE_LOW_LEVEL;
      break;
    case HIGH_LEVEL:
      type = GPIO_INT_TYPE_HIGH_LEVEL;
      break;
    default:
      /* BOTH_LEVELS has no encoding here: the pin selects one level, not
       * both, and a pin that interrupts on either level would never stop. */
      return RTEMS_UNSATISFIED;
  }

  reg = GPIO_REG( GPIO_PIN( pin ) );
  reg &= ~( GPIO_PIN_INT_TYPE_MASK | GPIO_PIN_INT_ENA_MASK );
  reg |= type << GPIO_PIN_INT_TYPE_SHIFT;
  reg |= GPIO_PIN_INT_ENA_CPU;
  GPIO_REG( GPIO_PIN( pin ) ) = reg;

  return RTEMS_SUCCESSFUL;
}

rtems_status_code rtems_gpio_bsp_disable_interrupt(
  RTEMS_UNUSED uint32_t bank,
  uint32_t pin,
  RTEMS_UNUSED rtems_gpio_interrupt active_interrupt
)
{
  uint32_t reg = GPIO_REG( GPIO_PIN( pin ) );

  reg &= ~( GPIO_PIN_INT_TYPE_MASK | GPIO_PIN_INT_ENA_MASK );
  GPIO_REG( GPIO_PIN( pin ) ) = reg;

  /* A level-triggered pin that is still at its trigger level re-asserts its
   * status bit until the type is cleared, which has just happened; drop what
   * it left behind so the next enable starts from no pending event. */
  GPIO_REG( GPIO_STATUS_W1TC ) = 1U << pin;

  return RTEMS_SUCCESSFUL;
}
