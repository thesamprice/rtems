/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSDeviceGPIOVirtual
 *
 * @brief This source file contains the implementation of the virtual GPIO
 *   controller.
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

#include <dev/gpio/gpio-virtual.h>

#include <rtems/score/basedefs.h>

#include <errno.h>
#include <string.h>

/*
 * Two invariants are established once, by rtems_gpio_virtual_check_pin()
 * at registration, and relied on everywhere below rather than rechecked:
 * a pin declaring RTEMS_GPIO_CAP_OUTPUT has a set callback, and one
 * declaring RTEMS_GPIO_CAP_INPUT has a get callback.  The generic layer
 * refuses a direction the pin did not declare, so a configured output has
 * a set callback and there is no path here that calls a null one.
 */

static rtems_gpio_virtual_ctrl*
rtems_gpio_virtual_downcast(rtems_gpio_drv_ctrl* ctrl) {
  return RTEMS_CONTAINER_OF(ctrl, rtems_gpio_virtual_ctrl, base);
}

static bool rtems_gpio_virtual_bit_get(const uint32_t* map, uint32_t pin) {
  return (map[pin / RTEMS_GPIO_BITMAP_WORD_BITS] &
          (1u << (pin % RTEMS_GPIO_BITMAP_WORD_BITS))) != 0;
}

static void rtems_gpio_virtual_bit_put(uint32_t* map, uint32_t pin,
                                       bool value) {
  uint32_t word = pin / RTEMS_GPIO_BITMAP_WORD_BITS;
  uint32_t bit = 1u << (pin % RTEMS_GPIO_BITMAP_WORD_BITS);

  if (value) {
    map[word] |= bit;
  } else {
    map[word] &= ~bit;
  }
}

/*
 * A bulk operation reaches the pins the caller's words cover and no more.
 * The bitmaps are the caller's storage and may be narrower than this
 * controller is wide.
 */
static uint32_t
rtems_gpio_virtual_bitmap_pins(const rtems_gpio_virtual_ctrl* self,
                               size_t words) {
  uint32_t pins = (uint32_t)(words * RTEMS_GPIO_BITMAP_WORD_BITS);

  return pins < self->base.pin_count ? pins : self->base.pin_count;
}

/*
 * An output whose pin does not claim RTEMS_GPIO_CAP_OUTPUT_READBACK reads
 * back the last level written and not the callback, which is what that
 * capability means.  So a clock gate needs no get callback to be readable,
 * and a proxy that claims readback is asked the other controller every
 * time.
 */
static bool
rtems_gpio_virtual_reads_shadow(const rtems_gpio_virtual_pin* pin,
                                const rtems_gpio_virtual_pin_state* state) {
  return state->config.direction == RTEMS_GPIO_DIRECTION_OUTPUT &&
         (pin->capabilities & RTEMS_GPIO_CAP_OUTPUT_READBACK) == 0;
}

static bool rtems_gpio_virtual_readable(const rtems_gpio_virtual_ctrl* self,
                                        uint32_t pin) {
  return rtems_gpio_virtual_reads_shadow(&self->pins[pin], &self->state[pin]) ||
         self->pins[pin].get != NULL;
}

static bool rtems_gpio_virtual_writable(const rtems_gpio_virtual_ctrl* self,
                                        uint32_t pin) {
  return self->state[pin].config.direction == RTEMS_GPIO_DIRECTION_OUTPUT;
}

static int rtems_gpio_virtual_read(rtems_gpio_virtual_ctrl* self, uint32_t pin,
                                   int* value) {
  const rtems_gpio_virtual_pin* entry = &self->pins[pin];
  rtems_gpio_virtual_pin_state* state = &self->state[pin];

  if (rtems_gpio_virtual_reads_shadow(entry, state)) {
    *value = state->level;

    return 0;
  }

  if (entry->get == NULL) {
    return ENOTSUP;
  }

  return (*entry->get)(pin, entry->arg, value);
}

static int rtems_gpio_virtual_write(rtems_gpio_virtual_ctrl* self, uint32_t pin,
                                    int value) {
  const rtems_gpio_virtual_pin* entry = &self->pins[pin];
  int err;

  if (!rtems_gpio_virtual_writable(self, pin)) {
    return ENOTSUP;
  }

  err = (*entry->set)(pin, entry->arg, value);
  if (err == 0) {
    self->state[pin].level = value;
  }

  return err;
}

static int rtems_gpio_virtual_pin_get_info(rtems_gpio_drv_ctrl* ctrl,
                                           uint32_t pin,
                                           rtems_gpio_pin_info* info) {
  rtems_gpio_virtual_ctrl* self = rtems_gpio_virtual_downcast(ctrl);
  const rtems_gpio_virtual_pin* entry = &self->pins[pin];

  /*
   * Every pin of this controller is one the driver provides, whatever is
   * on the far side of the callbacks.  A proxy onto a pad is still virtual
   * from here: the pad belongs to the controller that owns it.
   */
  info->kind = RTEMS_GPIO_PIN_VIRTUAL;
  info->capabilities = entry->capabilities;
  info->flags = entry->flags;

  if (self->state[pin].in_use) {
    info->flags |= RTEMS_GPIO_PIN_IN_USE;
  }

  if (entry->name != NULL) {
    strncpy(info->name, entry->name, sizeof(info->name) - 1);
  }

  if (entry->owner != NULL) {
    strncpy(info->owner, entry->owner, sizeof(info->owner) - 1);
  }

  return 0;
}

static int rtems_gpio_virtual_pin_configure(rtems_gpio_drv_ctrl* ctrl,
                                            uint32_t pin,
                                            rtems_gpio_config* config) {
  rtems_gpio_virtual_ctrl* self = rtems_gpio_virtual_downcast(ctrl);
  const rtems_gpio_virtual_pin* entry = &self->pins[pin];
  rtems_gpio_virtual_pin_state* state = &self->state[pin];

  /*
   * The level goes on before the pin counts as configured, so a callback
   * that refuses leaves the pin unconfigured rather than configured and
   * at an unknown level.  initial_value is physical here: the generic
   * layer converted it.
   */
  if (config->direction == RTEMS_GPIO_DIRECTION_OUTPUT) {
    int value = config->initial_value != 0;
    int err = (*entry->set)(pin, entry->arg, value);

    if (err != 0) {
      return err;
    }

    state->level = value;
  }

  state->config = *config;
  state->in_use = true;

  return 0;
}

static int rtems_gpio_virtual_pin_get_config(rtems_gpio_drv_ctrl* ctrl,
                                             uint32_t pin,
                                             rtems_gpio_config* config) {
  rtems_gpio_virtual_ctrl* self = rtems_gpio_virtual_downcast(ctrl);

  *config = self->state[pin].config;

  return 0;
}

static int rtems_gpio_virtual_pin_release(rtems_gpio_drv_ctrl* ctrl,
                                          uint32_t pin) {
  rtems_gpio_virtual_ctrl* self = rtems_gpio_virtual_downcast(ctrl);
  rtems_interrupt_lock_context lock_context;

  /*
   * The pin is left at whatever level it is at.  A clock gate that
   * switched off whatever it feeds because the last user let go of the pin
   * would be a surprise, and the registrant can do it from the set
   * callback if it wants it.
   */
  rtems_interrupt_lock_acquire(&self->lock, &lock_context);
  memset(&self->state[pin], 0, sizeof(self->state[pin]));
  rtems_interrupt_lock_release(&self->lock, &lock_context);

  return 0;
}

static int rtems_gpio_virtual_pin_get(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                                      int* value) {
  return rtems_gpio_virtual_read(rtems_gpio_virtual_downcast(ctrl), pin, value);
}

static int rtems_gpio_virtual_pin_set(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                                      int value) {
  return rtems_gpio_virtual_write(rtems_gpio_virtual_downcast(ctrl), pin,
                                  value != 0);
}

static int rtems_gpio_virtual_pin_toggle(rtems_gpio_drv_ctrl* ctrl,
                                         uint32_t pin) {
  rtems_gpio_virtual_ctrl* self = rtems_gpio_virtual_downcast(ctrl);
  int value;
  int err;

  err = rtems_gpio_virtual_read(self, pin, &value);
  if (err != 0) {
    return err;
  }

  return rtems_gpio_virtual_write(self, pin, value == 0);
}

static int rtems_gpio_virtual_pin_get_multiple(rtems_gpio_drv_ctrl* ctrl,
                                               const uint32_t* mask,
                                               uint32_t* values, size_t words) {
  rtems_gpio_virtual_ctrl* self = rtems_gpio_virtual_downcast(ctrl);
  uint32_t pins = rtems_gpio_virtual_bitmap_pins(self, words);
  uint32_t pin;

  /*
   * Every selected pin is checked before any callback runs, so a bitmap
   * naming one pin that cannot be read is refused rather than half
   * answered.  A callback that fails part way through cannot be undone,
   * and the values are undefined after that.
   */
  for (pin = 0; pin < pins; ++pin) {
    if (rtems_gpio_virtual_bit_get(mask, pin) &&
        !rtems_gpio_virtual_readable(self, pin)) {
      return ENOTSUP;
    }
  }

  for (pin = 0; pin < pins; ++pin) {
    int value;
    int err;

    if (!rtems_gpio_virtual_bit_get(mask, pin)) {
      continue;
    }

    err = rtems_gpio_virtual_read(self, pin, &value);
    if (err != 0) {
      return err;
    }

    rtems_gpio_virtual_bit_put(values, pin, value != 0);
  }

  return 0;
}

static int rtems_gpio_virtual_pin_set_multiple(rtems_gpio_drv_ctrl* ctrl,
                                               const uint32_t* mask,
                                               const uint32_t* values,
                                               size_t words) {
  rtems_gpio_virtual_ctrl* self = rtems_gpio_virtual_downcast(ctrl);
  uint32_t pins = rtems_gpio_virtual_bitmap_pins(self, words);
  uint32_t pin;

  for (pin = 0; pin < pins; ++pin) {
    if (rtems_gpio_virtual_bit_get(mask, pin) &&
        !rtems_gpio_virtual_writable(self, pin)) {
      return ENOTSUP;
    }
  }

  for (pin = 0; pin < pins; ++pin) {
    int err;

    if (!rtems_gpio_virtual_bit_get(mask, pin)) {
      continue;
    }

    err = rtems_gpio_virtual_write(self, pin,
                                   rtems_gpio_virtual_bit_get(values, pin));
    if (err != 0) {
      return err;
    }
  }

  return 0;
}

static int rtems_gpio_virtual_pin_irq_enable(rtems_gpio_drv_ctrl* ctrl,
                                             uint32_t pin,
                                             rtems_gpio_irq_handler handler,
                                             void* arg) {
  rtems_gpio_virtual_ctrl* self = rtems_gpio_virtual_downcast(ctrl);
  rtems_interrupt_lock_context lock_context;

  if (self->state[pin].config.trigger == RTEMS_GPIO_TRIGGER_NONE) {
    return EINVAL;
  }

  /*
   * Against rtems_gpio_virtual_raise(), which may run in interrupt
   * context and so cannot take the controller mutex this handler is
   * already under.
   */
  rtems_interrupt_lock_acquire(&self->lock, &lock_context);
  self->state[pin].handler = handler;
  self->state[pin].irq_arg = arg;
  rtems_interrupt_lock_release(&self->lock, &lock_context);

  return 0;
}

static int rtems_gpio_virtual_pin_irq_disable(rtems_gpio_drv_ctrl* ctrl,
                                              uint32_t pin) {
  rtems_gpio_virtual_ctrl* self = rtems_gpio_virtual_downcast(ctrl);
  rtems_interrupt_lock_context lock_context;

  rtems_interrupt_lock_acquire(&self->lock, &lock_context);
  self->state[pin].handler = NULL;
  self->state[pin].irq_arg = NULL;
  rtems_interrupt_lock_release(&self->lock, &lock_context);

  return 0;
}

static void rtems_gpio_virtual_destroy(rtems_gpio_drv_ctrl* ctrl) {
  rtems_gpio_virtual_ctrl* self = rtems_gpio_virtual_downcast(ctrl);

  /*
   * There is no lock object in a uniprocessor build, where the destroy
   * expands to nothing and does not name its argument.
   */
  (void)self;

  rtems_interrupt_lock_destroy(&self->lock);
}

static const rtems_gpio_drv_handlers rtems_gpio_virtual_handlers = {
    .pin_get_info = rtems_gpio_virtual_pin_get_info,
    .pin_configure = rtems_gpio_virtual_pin_configure,
    .pin_get_config = rtems_gpio_virtual_pin_get_config,
    .pin_release = rtems_gpio_virtual_pin_release,
    .pin_get = rtems_gpio_virtual_pin_get,
    .pin_set = rtems_gpio_virtual_pin_set,
    .pin_toggle = rtems_gpio_virtual_pin_toggle,
    .pin_get_multiple = rtems_gpio_virtual_pin_get_multiple,
    .pin_set_multiple = rtems_gpio_virtual_pin_set_multiple,
    .pin_irq_enable = rtems_gpio_virtual_pin_irq_enable,
    .pin_irq_disable = rtems_gpio_virtual_pin_irq_disable,
    .destroy = rtems_gpio_virtual_destroy};

/*
 * A capability with nothing behind it is refused at registration rather
 * than dropped from what the pin reports.  Dropping it would leave the
 * board describing a pin one way and the controller answering another,
 * and the first anyone would hear of it is an ENOTSUP from a configure
 * that the board says should work.
 */
static int rtems_gpio_virtual_check_pin(const rtems_gpio_virtual_pin* pin) {
  uint32_t caps = pin->capabilities;

  if ((caps & ~(uint32_t)RTEMS_GPIO_VIRTUAL_CAPS) != 0) {
    return EINVAL;
  }

  if ((caps & RTEMS_GPIO_CAP_OUTPUT) != 0 && pin->set == NULL) {
    return EINVAL;
  }

  if ((caps & RTEMS_GPIO_CAP_INPUT) != 0 && pin->get == NULL) {
    return EINVAL;
  }

  /* Readback is a property of an output, and it is the get that does it. */
  if ((caps & RTEMS_GPIO_CAP_OUTPUT_READBACK) != 0 &&
      ((caps & RTEMS_GPIO_CAP_OUTPUT) == 0 || pin->get == NULL)) {
    return EINVAL;
  }

  return 0;
}

int rtems_gpio_virtual_register(rtems_gpio_virtual_ctrl* ctrl,
                                const rtems_gpio_virtual_config* config,
                                const char* path) {
  uint32_t pin;
  int err;

  if (ctrl == NULL || config == NULL || path == NULL) {
    return EINVAL;
  }

  if (config->pins == NULL || config->state == NULL) {
    return EINVAL;
  }

  for (pin = 0; pin < config->pin_count; ++pin) {
    err = rtems_gpio_virtual_check_pin(&config->pins[pin]);
    if (err != 0) {
      return err;
    }
  }

  memset(ctrl, 0, sizeof(*ctrl));
  memset(config->state, 0, config->pin_count * sizeof(*config->state));

  ctrl->pins = config->pins;
  ctrl->state = config->state;
  ctrl->base.handlers = &rtems_gpio_virtual_handlers;
  ctrl->base.pin_count = config->pin_count;
  ctrl->base.name = config->name;
  ctrl->base.can_block = config->can_block;
  ctrl->base.active_low = config->active_low;
  ctrl->base.scratch = config->scratch;

  rtems_interrupt_lock_initialize(&ctrl->lock, "GPIO Virtual");

  /* The pin count and the two bitmaps are checked here and not above. */
  err = rtems_gpio_drv_ctrl_init(&ctrl->base);
  if (err != 0) {
    rtems_interrupt_lock_destroy(&ctrl->lock);

    return err;
  }

  /*
   * The generic layer calls the destroy handler when a registration
   * fails, so the lock is already gone and there is nothing to give back
   * here.
   */
  if (rtems_gpio_drv_ctrl_register(&ctrl->base, path) != 0) {
    return errno;
  }

  return 0;
}

int rtems_gpio_virtual_raise(rtems_gpio_virtual_ctrl* ctrl, uint32_t pin) {
  rtems_interrupt_lock_context lock_context;
  rtems_gpio_irq_handler handler;
  void* arg;

  if (ctrl == NULL) {
    return EINVAL;
  }

  if (pin >= ctrl->base.pin_count) {
    return ENODEV;
  }

  /*
   * The handler and its argument are taken together and the lock is
   * dropped before the call.  Holding it across the handler would run the
   * handler with interrupts disabled, which a controller whose can_block
   * is true has told its consumer is not the case.
   */
  rtems_interrupt_lock_acquire(&ctrl->lock, &lock_context);
  handler = ctrl->state[pin].handler;
  arg = ctrl->state[pin].irq_arg;
  rtems_interrupt_lock_release(&ctrl->lock, &lock_context);

  if (handler == NULL) {
    return ENOENT;
  }

  (*handler)(pin, arg);

  return 0;
}
