/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsRISCVESP32C3GPIO
 *
 * @brief This source file contains the implementation of the ESP32-C3 GPIO
 *   controller.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <bsp/esp32c3-gpio.h>
#include <bsp/irq.h>

/*
 * Pad arbitration, where the BSP has it.  <bsp/pin.h> is how this BSP's I2C,
 * UART and SPI drivers agree about pads, and a GPIO driver that did not join
 * in would be the one component able to take a pad out from under them.
 * Guarded because it is not in every tree carrying this BSP.
 */
#if defined( __has_include )
#if __has_include( <bsp/pin.h> )
#include <bsp/pin.h>
#define ESP32C3_GPIO_HAS_PIN_CLAIM 1
#endif
#endif

#include <rtems/score/basedefs.h>

#include <errno.h>
#include <string.h>

/*
 * The GPIO matrix and the IO MUX.  Two blocks: the matrix owns direction,
 * level and interrupt, and the IO MUX owns the pad itself -- which function
 * drives it, its pulls, its input buffer and its drive strength.  A pin has
 * to be set up in both, which is the part a driver written against only one
 * of them gets wrong.
 */
#define ESP32C3_GPIO_BASE  0x60004000u
#define ESP32C3_IOMUX_BASE 0x60009000u

#define ESP32C3_REG( addr ) ( *(volatile uint32_t *) (uintptr_t) ( addr ) )

#define ESP32C3_GPIO_OUT_W1TS    ( ESP32C3_GPIO_BASE + 0x008u )
#define ESP32C3_GPIO_OUT_W1TC    ( ESP32C3_GPIO_BASE + 0x00cu )
#define ESP32C3_GPIO_ENABLE_W1TS ( ESP32C3_GPIO_BASE + 0x024u )
#define ESP32C3_GPIO_ENABLE_W1TC ( ESP32C3_GPIO_BASE + 0x028u )
#define ESP32C3_GPIO_IN          ( ESP32C3_GPIO_BASE + 0x03cu )
#define ESP32C3_GPIO_STATUS      ( ESP32C3_GPIO_BASE + 0x044u )
#define ESP32C3_GPIO_STATUS_W1TC ( ESP32C3_GPIO_BASE + 0x04cu )
#define ESP32C3_GPIO_PIN( n )    ( ESP32C3_GPIO_BASE + 0x074u + ( n ) * 4u )
#define ESP32C3_GPIO_FUNC_OUT( n ) \
  ( ESP32C3_GPIO_BASE + 0x554u + ( n ) * 4u )
#define ESP32C3_IOMUX_PIN( n ) ( ESP32C3_IOMUX_BASE + 0x004u + ( n ) * 4u )

/* GPIO_PINn_REG */
#define ESP32C3_PIN_PAD_DRIVER  ( 1u << 2 )
#define ESP32C3_PIN_INT_TYPE_S  7
#define ESP32C3_PIN_INT_TYPE_M  ( 0x7u << ESP32C3_PIN_INT_TYPE_S )
#define ESP32C3_PIN_WAKEUP_ENA  ( 1u << 10 )
#define ESP32C3_PIN_INT_ENA_S   13
#define ESP32C3_PIN_INT_ENA_M   ( 0x1fu << ESP32C3_PIN_INT_ENA_S )
#define ESP32C3_PIN_INT_ENA_CPU ( 1u << ESP32C3_PIN_INT_ENA_S )

/* IO_MUX_GPIOn_REG */
#define ESP32C3_IOMUX_FUN_WPD  ( 1u << 7 )
#define ESP32C3_IOMUX_FUN_WPU  ( 1u << 8 )
#define ESP32C3_IOMUX_FUN_IE   ( 1u << 9 )
#define ESP32C3_IOMUX_FUN_DRV_S 10
#define ESP32C3_IOMUX_FUN_DRV_M ( 0x3u << ESP32C3_IOMUX_FUN_DRV_S )
#define ESP32C3_IOMUX_MCU_SEL_S 12
#define ESP32C3_IOMUX_MCU_SEL_M ( 0x7u << ESP32C3_IOMUX_MCU_SEL_S )

/* Function 1 is plain GPIO on every pad of this part. */
#define ESP32C3_IOMUX_FUNC_GPIO ( 1u << ESP32C3_IOMUX_MCU_SEL_S )

/* GPIO_FUNCn_OUT_SEL: 128 drives the pad from the GPIO output register. */
#define ESP32C3_FUNC_OUT_GPIO 128u

#define ESP32C3_GPIO_WORDS \
  RTEMS_GPIO_BITMAP_WORDS( ESP32C3_GPIO_PIN_COUNT )

/*
 * The four drive strengths the pad offers, in microamps, so that the generic
 * API's physical unit maps onto them rather than onto an index nobody outside
 * this file could interpret.  A request is rounded up to the first setting
 * that meets it, and reported back as what it actually got.
 */
static const uint32_t esp32c3_gpio_drive_ua[ 4 ] = {
  5000u, 10000u, 20000u, 40000u
};

/*
 * Facts about the part, which is why they are constants here and not build
 * options.  An earlier revision made both of these settable in config.ini
 * and that was wrong twice over.
 *
 * They are properties of the silicon rather than of a board: ESPHome keys
 * the same two sets on the chip variant and never on the board, and a value
 * a board could not legitimately change is not a setting.  And for pads
 * taken by a *driver* rather than by the part -- I2C, UART1, SPI, the
 * console -- a static mask is the mechanism that has already failed here
 * once: the equivalent table in docs/esp32c3-bsp.md listed GPIO7 and GPIO10
 * as free until the UART driver started using them, and nothing announced
 * that it had gone stale.  Those pads come from bsp_pin_owner() below, which
 * cannot.
 *
 * GPIO11 is deliberately absent.  It is VDD_SPI on modules that use the
 * internal regulator, but it is a usable pad on this board and ESPHome does
 * not reserve it either.
 */
#define ESP32C3_GPIO_FLASH_MASK     0x0003f000u /* GPIO12 to GPIO17 */
#define ESP32C3_GPIO_STRAPPING_MASK 0x00000304u /* GPIO2, GPIO8, GPIO9 */

/*
 * The console is reserved on top of whatever the board asked for, and which
 * pair it is follows the console the BSP was built with rather than a
 * separate setting that could disagree with it.
 */
#ifdef ESPRESSIF_USE_USB_CONSOLE
#define ESP32C3_GPIO_CONSOLE_MASK ( ( 1u << 18 ) | ( 1u << 19 ) )
#else
#define ESP32C3_GPIO_CONSOLE_MASK ( ( 1u << 20 ) | ( 1u << 21 ) )
#endif

#define ESP32C3_GPIO_ALL_RESERVED \
  ( ESP32C3_GPIO_FLASH_MASK | ESP32C3_GPIO_CONSOLE_MASK )

/* A mask bit above the last pad would reserve a pin that does not exist. */
RTEMS_STATIC_ASSERT(
  ( ( ESP32C3_GPIO_ALL_RESERVED | ESP32C3_GPIO_STRAPPING_MASK )
    >> ESP32C3_GPIO_PIN_COUNT ) == 0,
  esp32c3_gpio_mask_within_pin_count
);

/* A pad that is both is a contradiction: reserved means do not drive it. */
RTEMS_STATIC_ASSERT(
  ( ESP32C3_GPIO_ALL_RESERVED & ESP32C3_GPIO_STRAPPING_MASK ) == 0,
  esp32c3_gpio_masks_disjoint
);

/*
 * Names are not a build option.  A name is what the silkscreen and the
 * datasheet call the pad, so it is true whatever the board does with it, and
 * a board that reserves GPIO7 still wants to be told it reserved GPIO7 and
 * not a number.
 */
static const char *const esp32c3_gpio_names[ ESP32C3_GPIO_PIN_COUNT ] = {
  [ 2 ]  = "STRAP_BOOT2",
  [ 8 ]  = "STRAP_BOOT8",
  [ 9 ]  = "BOOT",
  [ 11 ] = "VDD_SPI",
  [ 12 ] = "SPIHD",
  [ 13 ] = "SPIWP",
  [ 14 ] = "SPICS0",
  [ 15 ] = "SPICLK",
  [ 16 ] = "SPID",
  [ 17 ] = "SPIQ",
  [ 18 ] = "USB_D-",
  [ 19 ] = "USB_D+",
  [ 20 ] = "U0RXD",
  [ 21 ] = "U0TXD"
};

/* What one pad's interrupt was asked to call. */
typedef struct {
  /** @brief This member contains the handler, or NULL. */
  rtems_gpio_irq_handler handler;
  /** @brief This member contains the argument given with the handler. */
  void                  *arg;
} esp32c3_gpio_irq;

/* The controller, with the storage the generic layer expects of a driver. */
typedef struct {
  /** @brief This member contains the generic controller. */
  rtems_gpio_drv_ctrl  base;
  /** @brief This member contains the logical polarity of each pin. */
  uint32_t         active_low[ ESP32C3_GPIO_WORDS ];
  /** @brief This member contains scratch space for a bulk write. */
  uint32_t         scratch[ ESP32C3_GPIO_WORDS ];
  /** @brief This member contains one bit per configured pin. */
  uint32_t         in_use;
  /** @brief This member contains each pin's interrupt handler. */
  esp32c3_gpio_irq irq[ ESP32C3_GPIO_PIN_COUNT ];
} esp32c3_gpio_ctrl;

static esp32c3_gpio_ctrl esp32c3_gpio_instance;

static esp32c3_gpio_ctrl *esp32c3_gpio_downcast( rtems_gpio_drv_ctrl *ctrl )
{
  return RTEMS_CONTAINER_OF( ctrl, esp32c3_gpio_ctrl, base );
}

/*
 * Every pad of this part can do all of this.  There is no per-pin variation
 * to express: what differs between pins is what the board has already
 * spoken for, which is the table above and not this.
 */
#define ESP32C3_GPIO_CAPS                                              \
  ( RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_OUTPUT |                     \
    RTEMS_GPIO_CAP_OUTPUT_READBACK | RTEMS_GPIO_CAP_PULL_UP |          \
    RTEMS_GPIO_CAP_PULL_DOWN | RTEMS_GPIO_CAP_OPEN_DRAIN |             \
    RTEMS_GPIO_CAP_DRIVE_STRENGTH | RTEMS_GPIO_CAP_EDGE_RISING |       \
    RTEMS_GPIO_CAP_EDGE_FALLING | RTEMS_GPIO_CAP_EDGE_BOTH |           \
    RTEMS_GPIO_CAP_LEVEL_HIGH | RTEMS_GPIO_CAP_LEVEL_LOW |             \
    RTEMS_GPIO_CAP_WAKEUP )

static int esp32c3_gpio_pin_get_info(
  rtems_gpio_drv_ctrl     *ctrl,
  uint32_t             pin,
  rtems_gpio_pin_info *info
)
{
  esp32c3_gpio_ctrl *self = esp32c3_gpio_downcast( ctrl );

  uint32_t bit = 1u << pin;

  info->kind = RTEMS_GPIO_PIN_PHYSICAL;
  info->capabilities = ESP32C3_GPIO_CAPS;
  info->flags = RTEMS_GPIO_PIN_AVAILABLE;

  if ( ( ESP32C3_GPIO_ALL_RESERVED & bit ) != 0 ) {
    info->flags |= RTEMS_GPIO_PIN_RESERVED;
  }

  if ( ( ESP32C3_GPIO_STRAPPING_MASK & bit ) != 0 ) {
    info->flags |= RTEMS_GPIO_PIN_STRAPPING;
  }

  if ( ( self->in_use & bit ) != 0 ) {
    info->flags |= RTEMS_GPIO_PIN_IN_USE;
  }

#ifdef ESP32C3_GPIO_HAS_PIN_CLAIM
  /*
   * A pad another driver has taken.  Asked rather than tabulated, so that a
   * bus this driver has never heard of still shows up, and so that the
   * answer cannot fall out of date with what is actually linked in.
   */
  {
    const char *owner = bsp_pin_owner( pin );

    if ( owner != NULL && ( self->in_use & bit ) == 0 ) {
      info->flags |= RTEMS_GPIO_PIN_RESERVED;
      strncpy( info->owner, owner, sizeof( info->owner ) - 1 );
    }
  }
#endif

  if ( ( info->flags & RTEMS_GPIO_PIN_RESERVED ) != 0
      && info->owner[ 0 ] == '\0' ) {
    strncpy( info->owner, "esp32c3 board", sizeof( info->owner ) - 1 );
  }

  if ( esp32c3_gpio_names[ pin ] != NULL ) {
    strncpy( info->name, esp32c3_gpio_names[ pin ], sizeof( info->name ) - 1 );
  }

  return 0;
}

static uint32_t esp32c3_gpio_drive_index( uint32_t microamps )
{
  uint32_t i;

  for ( i = 0; i < RTEMS_ARRAY_SIZE( esp32c3_gpio_drive_ua ) - 1; ++i ) {
    if ( microamps <= esp32c3_gpio_drive_ua[ i ] ) {
      break;
    }
  }

  return i;
}

static uint32_t esp32c3_gpio_int_type( rtems_gpio_trigger trigger )
{
  switch ( trigger ) {
    case RTEMS_GPIO_TRIGGER_EDGE_RISING:
      return 1u;
    case RTEMS_GPIO_TRIGGER_EDGE_FALLING:
      return 2u;
    case RTEMS_GPIO_TRIGGER_EDGE_BOTH:
      return 3u;
    case RTEMS_GPIO_TRIGGER_LEVEL_LOW:
      return 4u;
    case RTEMS_GPIO_TRIGGER_LEVEL_HIGH:
      return 5u;
    case RTEMS_GPIO_TRIGGER_NONE:
      break;
  }

  return 0u;
}

static int esp32c3_gpio_pin_configure(
  rtems_gpio_drv_ctrl   *ctrl,
  uint32_t           pin,
  rtems_gpio_config *config
)
{
  esp32c3_gpio_ctrl *self = esp32c3_gpio_downcast( ctrl );
  uint32_t           mux;
  uint32_t           cfg;
  uint32_t           drive;

#ifdef ESP32C3_GPIO_HAS_PIN_CLAIM
  /*
   * Before any register is touched, so a pad that belongs to another driver
   * is refused rather than half configured.  A re-claim by this driver of a
   * pad it already holds succeeds.
   */
  if ( !bsp_pin_claim( pin, "esp32c3 gpio" ) ) {
    return EACCES;
  }
#endif

  mux = ESP32C3_REG( ESP32C3_IOMUX_PIN( pin ) );
  mux &= ~( ESP32C3_IOMUX_MCU_SEL_M | ESP32C3_IOMUX_FUN_DRV_M
    | ESP32C3_IOMUX_FUN_WPU | ESP32C3_IOMUX_FUN_WPD | ESP32C3_IOMUX_FUN_IE );
  mux |= ESP32C3_IOMUX_FUNC_GPIO;

  /*
   * The input buffer stays on for an output too.  That is what makes a read
   * of an output return the pad rather than the latch, which is the whole
   * of RTEMS_GPIO_CAP_OUTPUT_READBACK on this part.
   */
  mux |= ESP32C3_IOMUX_FUN_IE;

  if ( config->bias == RTEMS_GPIO_BIAS_PULL_UP ) {
    mux |= ESP32C3_IOMUX_FUN_WPU;
  } else if ( config->bias == RTEMS_GPIO_BIAS_PULL_DOWN ) {
    mux |= ESP32C3_IOMUX_FUN_WPD;
  }

  drive = config->drive_strength != 0 ?
    esp32c3_gpio_drive_index( config->drive_strength ) : 2u;
  mux |= drive << ESP32C3_IOMUX_FUN_DRV_S;

  ESP32C3_REG( ESP32C3_IOMUX_PIN( pin ) ) = mux;

  /* Report what the pad can actually do, not what was asked for. */
  if ( config->drive_strength != 0 ) {
    config->drive_strength = esp32c3_gpio_drive_ua[ drive ];
  }

  cfg = ESP32C3_REG( ESP32C3_GPIO_PIN( pin ) );
  cfg &= ~( ESP32C3_PIN_PAD_DRIVER | ESP32C3_PIN_INT_TYPE_M
    | ESP32C3_PIN_INT_ENA_M | ESP32C3_PIN_WAKEUP_ENA );

  if ( config->drive == RTEMS_GPIO_DRIVE_OPEN_DRAIN ) {
    cfg |= ESP32C3_PIN_PAD_DRIVER;
  }

  cfg |= esp32c3_gpio_int_type( config->trigger ) << ESP32C3_PIN_INT_TYPE_S;

  if ( ( config->flags & RTEMS_GPIO_FLAG_WAKEUP ) != 0 ) {
    cfg |= ESP32C3_PIN_WAKEUP_ENA;
  }

  ESP32C3_REG( ESP32C3_GPIO_PIN( pin ) ) = cfg;

  if ( config->direction == RTEMS_GPIO_DIRECTION_OUTPUT ) {
    /*
     * The level before the driver is enabled, so that enabling the output
     * does not drive the previous contents of the register at the pad for
     * as long as it takes to get to the next write.  config->initial_value
     * is already a physical level: the generic layer applied
     * RTEMS_GPIO_FLAG_ACTIVE_LOW on the way in.
     */
    ESP32C3_REG(
      config->initial_value != 0 ?
        ESP32C3_GPIO_OUT_W1TS : ESP32C3_GPIO_OUT_W1TC
    ) = 1u << pin;
    ESP32C3_REG( ESP32C3_GPIO_FUNC_OUT( pin ) ) = ESP32C3_FUNC_OUT_GPIO;
    ESP32C3_REG( ESP32C3_GPIO_ENABLE_W1TS ) = 1u << pin;
  } else {
    ESP32C3_REG( ESP32C3_GPIO_ENABLE_W1TC ) = 1u << pin;
  }

  self->in_use |= 1u << pin;

  return 0;
}

static int esp32c3_gpio_pin_release( rtems_gpio_drv_ctrl *ctrl, uint32_t pin )
{
  esp32c3_gpio_ctrl *self = esp32c3_gpio_downcast( ctrl );
  uint32_t           cfg;

  ESP32C3_REG( ESP32C3_GPIO_ENABLE_W1TC ) = 1u << pin;

  cfg = ESP32C3_REG( ESP32C3_GPIO_PIN( pin ) );
  cfg &= ~( ESP32C3_PIN_INT_TYPE_M | ESP32C3_PIN_INT_ENA_M
    | ESP32C3_PIN_WAKEUP_ENA | ESP32C3_PIN_PAD_DRIVER );
  ESP32C3_REG( ESP32C3_GPIO_PIN( pin ) ) = cfg;

  self->irq[ pin ].handler = NULL;
  self->in_use &= ~( 1u << pin );

#ifdef ESP32C3_GPIO_HAS_PIN_CLAIM
  bsp_pin_release( pin );
#endif

  return 0;
}

static int esp32c3_gpio_pin_get(
  rtems_gpio_drv_ctrl *ctrl,
  uint32_t         pin,
  int             *value
)
{
  (void) ctrl;

  *value = ( ESP32C3_REG( ESP32C3_GPIO_IN ) >> pin ) & 1u;

  return 0;
}

static int esp32c3_gpio_pin_set(
  rtems_gpio_drv_ctrl *ctrl,
  uint32_t         pin,
  int              value
)
{
  (void) ctrl;

  ESP32C3_REG(
    value != 0 ? ESP32C3_GPIO_OUT_W1TS : ESP32C3_GPIO_OUT_W1TC
  ) = 1u << pin;

  return 0;
}

static int esp32c3_gpio_pin_toggle( rtems_gpio_drv_ctrl *ctrl, uint32_t pin )
{
  (void) ctrl;

  if ( ( ( ESP32C3_REG( ESP32C3_GPIO_IN ) >> pin ) & 1u ) != 0 ) {
    ESP32C3_REG( ESP32C3_GPIO_OUT_W1TC ) = 1u << pin;
  } else {
    ESP32C3_REG( ESP32C3_GPIO_OUT_W1TS ) = 1u << pin;
  }

  return 0;
}

/*
 * The reason the bulk API is a bitmap rather than a list of pins.  This part
 * has 22 pads in one register, so a whole transfer is one mask and one
 * write-one-to-set plus one write-one-to-clear -- no loop over pins, and no
 * read-modify-write to race with an interrupt handler touching another pad.
 * A part with more pads than fit in a register would do the same thing once
 * per word, up to the caller's word count and no further.
 */
static int esp32c3_gpio_pin_get_multiple(
  rtems_gpio_drv_ctrl *ctrl,
  const uint32_t  *mask,
  uint32_t        *values,
  size_t           words
)
{
  (void) ctrl;

  /*
   * All 22 pads are in word 0, and the caller always provides at least one
   * word, so there is nothing here to clamp.  A wider part would loop to
   * words and stop, because past that is storage the caller never allocated.
   */
  (void) words;

  values[ 0 ] = ESP32C3_REG( ESP32C3_GPIO_IN ) & mask[ 0 ];

  return 0;
}

static int esp32c3_gpio_pin_set_multiple(
  rtems_gpio_drv_ctrl *ctrl,
  const uint32_t  *mask,
  const uint32_t  *values,
  size_t           words
)
{
  (void) ctrl;
  (void) words;

  ESP32C3_REG( ESP32C3_GPIO_OUT_W1TS ) = mask[ 0 ] & values[ 0 ];
  ESP32C3_REG( ESP32C3_GPIO_OUT_W1TC ) = mask[ 0 ] & ~values[ 0 ];

  return 0;
}

/*
 * One interrupt for the whole controller, so the handler reads the status
 * register and calls whoever asked.  The status bit is cleared before the
 * handler runs, so an edge that arrives during it is not lost.
 */
static void esp32c3_gpio_isr( void *arg )
{
  esp32c3_gpio_ctrl *self = arg;
  uint32_t           status;
  uint32_t           pin;

  status = ESP32C3_REG( ESP32C3_GPIO_STATUS );
  ESP32C3_REG( ESP32C3_GPIO_STATUS_W1TC ) = status;

  for ( pin = 0; pin < ESP32C3_GPIO_PIN_COUNT; ++pin ) {
    if ( ( status & ( 1u << pin ) ) == 0 ) {
      continue;
    }

    if ( self->irq[ pin ].handler != NULL ) {
      ( *self->irq[ pin ].handler )( pin, self->irq[ pin ].arg );
    }
  }
}

static int esp32c3_gpio_pin_irq_enable(
  rtems_gpio_drv_ctrl       *ctrl,
  uint32_t               pin,
  rtems_gpio_irq_handler handler,
  void                  *arg
)
{
  esp32c3_gpio_ctrl *self = esp32c3_gpio_downcast( ctrl );
  uint32_t           cfg;

  cfg = ESP32C3_REG( ESP32C3_GPIO_PIN( pin ) );

  /* A pin configured RTEMS_GPIO_TRIGGER_NONE has nothing to deliver. */
  if ( ( cfg & ESP32C3_PIN_INT_TYPE_M ) == 0 ) {
    return EINVAL;
  }

  self->irq[ pin ].handler = handler;
  self->irq[ pin ].arg = arg;

  ESP32C3_REG( ESP32C3_GPIO_STATUS_W1TC ) = 1u << pin;
  ESP32C3_REG( ESP32C3_GPIO_PIN( pin ) ) = cfg | ESP32C3_PIN_INT_ENA_CPU;

  return 0;
}

static int esp32c3_gpio_pin_irq_disable( rtems_gpio_drv_ctrl *ctrl, uint32_t pin )
{
  esp32c3_gpio_ctrl *self = esp32c3_gpio_downcast( ctrl );
  uint32_t           cfg;

  cfg = ESP32C3_REG( ESP32C3_GPIO_PIN( pin ) );
  ESP32C3_REG( ESP32C3_GPIO_PIN( pin ) ) = cfg & ~ESP32C3_PIN_INT_ENA_M;

  self->irq[ pin ].handler = NULL;

  return 0;
}

static const rtems_gpio_drv_handlers esp32c3_gpio_handlers = {
  .pin_get_info = esp32c3_gpio_pin_get_info,
  .pin_configure = esp32c3_gpio_pin_configure,
  .pin_release = esp32c3_gpio_pin_release,
  .pin_get = esp32c3_gpio_pin_get,
  .pin_set = esp32c3_gpio_pin_set,
  .pin_toggle = esp32c3_gpio_pin_toggle,
  .pin_get_multiple = esp32c3_gpio_pin_get_multiple,
  .pin_set_multiple = esp32c3_gpio_pin_set_multiple,
  .pin_irq_enable = esp32c3_gpio_pin_irq_enable,
  .pin_irq_disable = esp32c3_gpio_pin_irq_disable
};

int esp32c3_gpio_register( const char *path )
{
  esp32c3_gpio_ctrl *self = &esp32c3_gpio_instance;
  rtems_status_code  sc;
  int                err;

  memset( self, 0, sizeof( *self ) );

  self->base.handlers = &esp32c3_gpio_handlers;
  self->base.pin_count = ESP32C3_GPIO_PIN_COUNT;
  self->base.name = "esp32c3-gpio";

  /*
   * Memory mapped registers, so nothing here waits on anything and a caller
   * may use this controller from interrupt context.  An expander behind I2C
   * would say true and a caller would have to keep off it in an ISR.
   */
  self->base.can_block = false;

  /* Storage the generic layer keeps logical polarity in. */
  self->base.active_low = self->active_low;
  self->base.scratch = self->scratch;

  err = rtems_gpio_drv_ctrl_init( &self->base );
  if ( err != 0 ) {
    return err;
  }

  sc = rtems_interrupt_handler_install(
    GPIO_PROCPU_INTR,
    "esp32c3-gpio",
    RTEMS_INTERRUPT_SHARED,
    esp32c3_gpio_isr,
    self
  );
  if ( sc != RTEMS_SUCCESSFUL ) {
    return EIO;
  }

  return rtems_gpio_drv_ctrl_register( &self->base, path );
}
