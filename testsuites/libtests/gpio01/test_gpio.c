/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @brief A GPIO controller with no hardware behind it, for gpio01.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "test_gpio.h"

#include <errno.h>
#include <string.h>

/*
 * This models a part, not a board: what a pin can do is fixed in
 * test_gpio_pins, what a pin is currently set to lives in test_gpio_state.
 * Keeping them apart is the same split the API draws between
 * rtems_gpio_pin_get_info() and rtems_gpio_pin_get_configuration(), and
 * having the simulator obey it is most of what makes the test meaningful.
 */

typedef struct {
  uint32_t            capabilities;
  uint32_t            flags;
  rtems_gpio_pin_kind kind;
  const char         *name;
} test_gpio_pin;

#define TEST_GPIO_CAP_FULL                                             \
  ( RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_OUTPUT |                     \
    RTEMS_GPIO_CAP_BIDIRECTIONAL | RTEMS_GPIO_CAP_PULL_UP |            \
    RTEMS_GPIO_CAP_PULL_DOWN | RTEMS_GPIO_CAP_OPEN_DRAIN |             \
    RTEMS_GPIO_CAP_DRIVE_STRENGTH | RTEMS_GPIO_CAP_DEBOUNCE |          \
    RTEMS_GPIO_CAP_INVERT | RTEMS_GPIO_CAP_EDGE_RISING |               \
    RTEMS_GPIO_CAP_EDGE_FALLING | RTEMS_GPIO_CAP_EDGE_BOTH )

static const test_gpio_pin test_gpio_pins[ TEST_GPIO_PIN_COUNT ] = {
  [ TEST_GPIO_PIN_FULL ] = {
    .capabilities = TEST_GPIO_CAP_FULL,
    .flags = RTEMS_GPIO_PIN_AVAILABLE,
    .kind = RTEMS_GPIO_PIN_PHYSICAL,
    .name = "LED0"
  },
  [ TEST_GPIO_PIN_FULL2 ] = {
    .capabilities = TEST_GPIO_CAP_FULL,
    .flags = RTEMS_GPIO_PIN_AVAILABLE,
    .kind = RTEMS_GPIO_PIN_PHYSICAL,
    .name = ""
  },
  [ TEST_GPIO_PIN_FULL3 ] = {
    .capabilities = TEST_GPIO_CAP_FULL,
    .flags = RTEMS_GPIO_PIN_AVAILABLE,
    .kind = RTEMS_GPIO_PIN_PHYSICAL,
    .name = ""
  },
  [ TEST_GPIO_PIN_INPUT ] = {
    .capabilities = RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_PULL_UP,
    .flags = RTEMS_GPIO_PIN_AVAILABLE,
    .kind = RTEMS_GPIO_PIN_PHYSICAL,
    .name = ""
  },
  [ TEST_GPIO_PIN_RESERVED ] = {
    /*
     * Capable of everything and permitted for nothing, which is exactly the
     * case that matters: a flash pad is an ordinary pad electrically.  If
     * the reserved flag were only set on a pin that could not do anything
     * anyway, the test would pass without the check it is there to prove.
     */
    .capabilities = TEST_GPIO_CAP_FULL,
    .flags = RTEMS_GPIO_PIN_RESERVED,
    .kind = RTEMS_GPIO_PIN_PHYSICAL,
    .name = "FLASH_CLK"
  },
  [ TEST_GPIO_PIN_NO_PAD ] = {
    .capabilities = TEST_GPIO_CAP_FULL,
    .flags = RTEMS_GPIO_PIN_NO_PAD,
    .kind = RTEMS_GPIO_PIN_PHYSICAL,
    .name = ""
  },
  [ TEST_GPIO_PIN_STRAPPING ] = {
    .capabilities = TEST_GPIO_CAP_FULL,
    .flags = RTEMS_GPIO_PIN_STRAPPING,
    .kind = RTEMS_GPIO_PIN_PHYSICAL,
    .name = "BOOT"
  },
  [ TEST_GPIO_PIN_VIRTUAL ] = {
    /* An expander behind a bus: it has pins and pulls, and nothing else. */
    .capabilities = RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_OUTPUT |
      RTEMS_GPIO_CAP_PULL_UP,
    .flags = RTEMS_GPIO_PIN_AVAILABLE,
    .kind = RTEMS_GPIO_PIN_VIRTUAL,
    .name = "EXP0"
  }
};

typedef struct {
  bool                   in_use;
  rtems_gpio_config      config;
  int                    level;  /* the pad, not the logical value */
  rtems_gpio_irq_handler handler;
  void                  *arg;
} test_gpio_pin_state;

typedef struct {
  rtems_gpio_ctrl     base;
  test_gpio_pin_state pins[ TEST_GPIO_PIN_COUNT ];
} test_gpio_ctrl;

static test_gpio_ctrl test_gpio_instance;
static test_gpio_ctrl test_gpio_minimal_instance;

static test_gpio_ctrl *test_gpio_downcast( rtems_gpio_ctrl *ctrl )
{
  return RTEMS_CONTAINER_OF( ctrl, test_gpio_ctrl, base );
}

/*
 * Inversion is applied here, in the driver, and not in the generic layer.
 * The API says a level is logical and a driver that reports
 * RTEMS_GPIO_CAP_INVERT undertakes to do the inverting; a driver whose
 * hardware inverts for it would do nothing at all in these two functions,
 * which is why the generic layer cannot do it on everyone's behalf.
 */
static int test_gpio_to_pad( const test_gpio_pin_state *state, int value )
{
  if ( ( state->config.flags & RTEMS_GPIO_FLAG_ACTIVE_LOW ) != 0 ) {
    return value != 0 ? 0 : 1;
  }

  return value != 0 ? 1 : 0;
}

static int test_gpio_from_pad( const test_gpio_pin_state *state, int level )
{
  if ( ( state->config.flags & RTEMS_GPIO_FLAG_ACTIVE_LOW ) != 0 ) {
    return level != 0 ? 0 : 1;
  }

  return level != 0 ? 1 : 0;
}

static int test_gpio_pin_get_info(
  rtems_gpio_ctrl     *ctrl,
  uint32_t             pin,
  rtems_gpio_pin_info *info
)
{
  test_gpio_ctrl *self = test_gpio_downcast( ctrl );

  info->pin = pin;
  info->kind = test_gpio_pins[ pin ].kind;
  info->capabilities = test_gpio_pins[ pin ].capabilities;
  info->flags = test_gpio_pins[ pin ].flags;

  if ( self->pins[ pin ].in_use ) {
    info->flags |= RTEMS_GPIO_PIN_IN_USE;
  }

  strncpy( info->name, test_gpio_pins[ pin ].name, sizeof( info->name ) - 1 );

  return 0;
}

static int test_gpio_pin_configure(
  rtems_gpio_ctrl   *ctrl,
  uint32_t           pin,
  rtems_gpio_config *config
)
{
  test_gpio_ctrl      *self = test_gpio_downcast( ctrl );
  test_gpio_pin_state *state = &self->pins[ pin ];

  /*
   * Rounded to what this "part" has, and written back, so that a caller
   * asking for 8000 uA is told it got 10000 rather than being left to
   * assume it got what it asked for.
   */
  if ( config->drive_strength != 0 ) {
    config->drive_strength = TEST_GPIO_DRIVE_STRENGTH;
  }

  state->config = *config;
  state->in_use = true;

  if (
    config->direction == RTEMS_GPIO_DIRECTION_OUTPUT
      || config->direction == RTEMS_GPIO_DIRECTION_BIDIRECTIONAL
  ) {
    state->level = test_gpio_to_pad( state, config->initial_value );
  }

  return 0;
}

static int test_gpio_pin_get_config(
  rtems_gpio_ctrl   *ctrl,
  uint32_t           pin,
  rtems_gpio_config *config
)
{
  test_gpio_ctrl *self = test_gpio_downcast( ctrl );

  *config = self->pins[ pin ].config;

  return 0;
}

static int test_gpio_pin_release( rtems_gpio_ctrl *ctrl, uint32_t pin )
{
  test_gpio_ctrl *self = test_gpio_downcast( ctrl );

  memset( &self->pins[ pin ], 0, sizeof( self->pins[ pin ] ) );

  return 0;
}

static int test_gpio_pin_get( rtems_gpio_ctrl *ctrl, uint32_t pin, int *value )
{
  test_gpio_ctrl      *self = test_gpio_downcast( ctrl );
  test_gpio_pin_state *state = &self->pins[ pin ];

  if ( state->config.direction == RTEMS_GPIO_DIRECTION_OUTPUT ) {
    return ENOTSUP;
  }

  *value = test_gpio_from_pad( state, state->level );

  return 0;
}

static int test_gpio_pin_set( rtems_gpio_ctrl *ctrl, uint32_t pin, int value )
{
  test_gpio_ctrl      *self = test_gpio_downcast( ctrl );
  test_gpio_pin_state *state = &self->pins[ pin ];

  if ( state->config.direction == RTEMS_GPIO_DIRECTION_INPUT ) {
    return ENOTSUP;
  }

  state->level = test_gpio_to_pad( state, value );

  return 0;
}

static int test_gpio_pin_toggle( rtems_gpio_ctrl *ctrl, uint32_t pin )
{
  test_gpio_ctrl      *self = test_gpio_downcast( ctrl );
  test_gpio_pin_state *state = &self->pins[ pin ];

  if ( state->config.direction == RTEMS_GPIO_DIRECTION_INPUT ) {
    return ENOTSUP;
  }

  state->level = state->level != 0 ? 0 : 1;

  return 0;
}

static int test_gpio_pin_get_multiple(
  rtems_gpio_ctrl    *ctrl,
  rtems_gpio_pin_list *list
)
{
  test_gpio_ctrl *self = test_gpio_downcast( ctrl );
  uint32_t        i;

  list->values = 0;

  for ( i = 0; i < list->count; ++i ) {
    test_gpio_pin_state *state = &self->pins[ list->pins[ i ] ];

    if ( test_gpio_from_pad( state, state->level ) != 0 ) {
      list->values |= 1u << i;
    }
  }

  return 0;
}

static int test_gpio_pin_set_multiple(
  rtems_gpio_ctrl           *ctrl,
  const rtems_gpio_pin_list *list
)
{
  test_gpio_ctrl *self = test_gpio_downcast( ctrl );
  uint32_t        i;

  for ( i = 0; i < list->count; ++i ) {
    test_gpio_pin_state *state = &self->pins[ list->pins[ i ] ];

    state->level = test_gpio_to_pad( state, ( list->values >> i ) & 1u );
  }

  return 0;
}

static int test_gpio_pin_irq_enable(
  rtems_gpio_ctrl       *ctrl,
  uint32_t               pin,
  rtems_gpio_irq_handler handler,
  void                  *arg
)
{
  test_gpio_ctrl      *self = test_gpio_downcast( ctrl );
  test_gpio_pin_state *state = &self->pins[ pin ];

  if ( state->config.trigger == RTEMS_GPIO_TRIGGER_NONE ) {
    return EINVAL;
  }

  state->handler = handler;
  state->arg = arg;

  return 0;
}

static int test_gpio_pin_irq_disable( rtems_gpio_ctrl *ctrl, uint32_t pin )
{
  test_gpio_ctrl *self = test_gpio_downcast( ctrl );

  self->pins[ pin ].handler = NULL;
  self->pins[ pin ].arg = NULL;

  return 0;
}

static const rtems_gpio_handlers test_gpio_handlers = {
  .pin_get_info = test_gpio_pin_get_info,
  .pin_configure = test_gpio_pin_configure,
  .pin_get_config = test_gpio_pin_get_config,
  .pin_release = test_gpio_pin_release,
  .pin_get = test_gpio_pin_get,
  .pin_set = test_gpio_pin_set,
  .pin_toggle = test_gpio_pin_toggle,
  .pin_get_multiple = test_gpio_pin_get_multiple,
  .pin_set_multiple = test_gpio_pin_set_multiple,
  .pin_irq_enable = test_gpio_pin_irq_enable,
  .pin_irq_disable = test_gpio_pin_irq_disable,
  .destroy = NULL
};

/*
 * Everything but pin_get_info() left NULL.  A driver is allowed to be this
 * incomplete, and the generic layer has to answer for it rather than call
 * through a null pointer, which is what the ENOTSUP half of the test
 * checks.
 */
static const rtems_gpio_handlers test_gpio_minimal_handlers = {
  .pin_get_info = test_gpio_pin_get_info
};

int test_gpio_register( const char *path )
{
  int err;

  memset( &test_gpio_instance, 0, sizeof( test_gpio_instance ) );

  test_gpio_instance.base.handlers = &test_gpio_handlers;
  test_gpio_instance.base.pin_count = TEST_GPIO_PIN_COUNT;
  test_gpio_instance.base.name = "test-gpio";

  err = rtems_gpio_ctrl_init( &test_gpio_instance.base );
  if ( err != 0 ) {
    return err;
  }

  return rtems_gpio_ctrl_register( &test_gpio_instance.base, path );
}

int test_gpio_register_minimal( const char *path )
{
  int err;

  memset( &test_gpio_minimal_instance, 0, sizeof( test_gpio_minimal_instance ) );

  test_gpio_minimal_instance.base.handlers = &test_gpio_minimal_handlers;
  test_gpio_minimal_instance.base.pin_count = TEST_GPIO_PIN_COUNT;
  test_gpio_minimal_instance.base.name = "test-gpio-min";

  err = rtems_gpio_ctrl_init( &test_gpio_minimal_instance.base );
  if ( err != 0 ) {
    return err;
  }

  return rtems_gpio_ctrl_register( &test_gpio_minimal_instance.base, path );
}

int test_gpio_raw_level( uint32_t pin )
{
  return test_gpio_instance.pins[ pin ].level;
}

void test_gpio_set_raw_level( uint32_t pin, int value )
{
  test_gpio_instance.pins[ pin ].level = value != 0 ? 1 : 0;
}

void test_gpio_fire_irq( uint32_t pin )
{
  test_gpio_pin_state *state = &test_gpio_instance.pins[ pin ];

  if ( state->handler != NULL ) {
    ( *state->handler )( pin, state->arg );
  }
}
