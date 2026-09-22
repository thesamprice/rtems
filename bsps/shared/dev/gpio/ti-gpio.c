/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSDeviceTIGPIO
 *
 * @brief This source file contains the implementation of the Texas
 *   Instruments OMAP GPIO controller.
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

#include <dev/gpio/ti-gpio.h>

#include <rtems/irq-extension.h>
#include <rtems/score/basedefs.h>

#include <errno.h>
#include <libfdt.h>
#include <string.h>

/*
 * The part has several of these blocks rather than one wide one, each with
 * its own register base, its own interrupt and its own node in the device
 * tree.  So one node becomes one controller: the pin numbers then agree
 * with what the manual, the board silkscreen and a Linux device tree all
 * call the line, and a block the tree disables is simply absent instead of
 * leaving a hole in a pin space the API says has none.
 */

#define TI_GPIO_WORDS RTEMS_GPIO_BITMAP_WORDS(TI_GPIO_PIN_COUNT_MAX)

/* Register offsets, from the AM335x reference manual. */
#define TI_GPIO_IRQSTATUS_0 0x02cu
#define TI_GPIO_IRQSTATUS_SET_0 0x034u
#define TI_GPIO_IRQSTATUS_CLR_0 0x03cu
#define TI_GPIO_CTRL 0x130u
#define TI_GPIO_OE 0x134u
#define TI_GPIO_DATAIN 0x138u
#define TI_GPIO_DATAOUT 0x13cu
#define TI_GPIO_LEVELDETECT0 0x140u
#define TI_GPIO_LEVELDETECT1 0x144u
#define TI_GPIO_RISINGDETECT 0x148u
#define TI_GPIO_FALLINGDETECT 0x14cu
#define TI_GPIO_CLEARDATAOUT 0x190u
#define TI_GPIO_SETDATAOUT 0x194u

/* The module is held in reset while this is set. */
#define TI_GPIO_CTRL_DISABLEMODULE (1u << 0)

#define TI_GPIO_REG(self, off) (*(volatile uint32_t*)((self)->regs + (off)))

/*
 * What the block can do for every pin.  There is no per-pin variation
 * here; what differs between pins is what the board has spoken for, and
 * that comes from the device tree.
 */
#define TI_GPIO_CAPS                                                           \
  (RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_OUTPUT | RTEMS_GPIO_CAP_EDGE_RISING | \
   RTEMS_GPIO_CAP_EDGE_FALLING | RTEMS_GPIO_CAP_EDGE_BOTH |                    \
   RTEMS_GPIO_CAP_LEVEL_HIGH | RTEMS_GPIO_CAP_LEVEL_LOW)

/*
 * The pull resistors and the slew rate are in the control module and not
 * in this block, the part has neither open-drain nor a selectable drive
 * strength, the debounce counter runs from a clock the power and clock
 * manager gates and this driver cannot turn on, and nothing here wakes a
 * system that has no low power state to wake from.  Claiming any of them
 * would make the generic layer accept a configuration that nothing below
 * it would then apply.
 */
#define TI_GPIO_CAPS_ELSEWHERE                                                 \
  (RTEMS_GPIO_CAP_PULL_UP | RTEMS_GPIO_CAP_PULL_DOWN |                         \
   RTEMS_GPIO_CAP_OPEN_DRAIN | RTEMS_GPIO_CAP_OPEN_SOURCE |                    \
   RTEMS_GPIO_CAP_DRIVE_STRENGTH | RTEMS_GPIO_CAP_DEBOUNCE |                   \
   RTEMS_GPIO_CAP_WAKEUP)

/*
 * A read of an output returns what the pin is driven to and not what the
 * pad is held at, so the block does not read back in the sense the API
 * means.
 */
#define TI_GPIO_CAPS_NOT_CLAIMED                                               \
  (TI_GPIO_CAPS_ELSEWHERE | RTEMS_GPIO_CAP_OUTPUT_READBACK)

RTEMS_STATIC_ASSERT((TI_GPIO_CAPS & TI_GPIO_CAPS_NOT_CLAIMED) == 0,
                    ti_gpio_claims_only_what_this_block_has);

RTEMS_STATIC_ASSERT(TI_GPIO_PIN_COUNT_MAX == RTEMS_GPIO_BITMAP_WORD_BITS,
                    ti_gpio_one_block_is_one_bitmap_word);

/* The controller number goes into a path as one digit. */
RTEMS_STATIC_ASSERT(TI_GPIO_CTRL_MAX <= 10, ti_gpio_number_is_one_digit);

/* What the device tree said about one pin, and who wants its interrupt. */
typedef struct {
  /**
   * @brief This member contains the pin's "rtems,label", or NULL.
   */
  const char* label;

  /**
   * @brief This member contains the pin's "rtems,owner", or NULL.
   */
  const char* owner;

  /**
   * @brief This member contains the pin's interrupt handler, or NULL.
   */
  rtems_gpio_irq_handler handler;

  /**
   * @brief This member contains the argument given with the handler.
   */
  void* arg;
} ti_gpio_pin;

/*
 * One block.  Every set of pins here is a plain word because every
 * register in the block is one word covering all 32 pins, so a mask goes
 * straight into one.
 */
typedef struct {
  /**
   * @brief This member contains the generic controller.
   */
  rtems_gpio_drv_ctrl base;

  /**
   * @brief This member contains the base address of the register block.
   */
  uintptr_t regs;

  /**
   * @brief This member contains the pins the controller publishes.
   */
  uint32_t pin_mask;

  /**
   * @brief This member contains one bit per configured pin.
   */
  uint32_t in_use;

  /**
   * @brief This member contains the pins whose level must be read from the
   *   output register rather than from the pad.
   */
  uint32_t out_read;

  /**
   * @brief This member contains the pins configured active low.
   */
  uint32_t inverted;

  /**
   * @brief This member contains the pins configured with a trigger.
   */
  uint32_t has_trigger;

  /**
   * @brief This member contains the pins the device tree reserves.
   */
  uint32_t reserved;

  /**
   * @brief This member contains the pins the device tree marks strapping.
   */
  uint32_t strapping;

  /**
   * @brief This member contains the pins the device tree says reach no pad.
   */
  uint32_t no_pad;

  /**
   * @brief This member contains the polarity bitmap the generic layer
   *   maintains.
   */
  uint32_t active_low[TI_GPIO_WORDS];

  /**
   * @brief This member contains the scratch bitmap the generic layer needs
   *   for a bulk write.
   */
  uint32_t scratch[TI_GPIO_WORDS];

  /**
   * @brief This member contains the interrupt vector, valid while has_irq.
   */
  rtems_vector_number vector;

  /**
   * @brief This member is true, if an interrupt handler was installed,
   *   otherwise false.
   */
  bool has_irq;

  /**
   * @brief This member contains what this controller calls itself.
   */
  char name[RTEMS_GPIO_NAME_MAX];

  /**
   * @brief This member contains what the device tree said about each pin.
   */
  ti_gpio_pin pins[TI_GPIO_PIN_COUNT_MAX];
} ti_gpio_ctrl;

static ti_gpio_ctrl ti_gpio_instances[TI_GPIO_CTRL_MAX];

static size_t ti_gpio_instance_count;

static ti_gpio_ctrl* ti_gpio_downcast(rtems_gpio_drv_ctrl* ctrl) {
  return RTEMS_CONTAINER_OF(ctrl, ti_gpio_ctrl, base);
}

/*
 * Every block looks alike, so the register base is what tells two of them
 * apart in a diagnostic.  It is also what the device tree node is named
 * after in a tree that has not been through the target module rewrite.
 */
static void ti_gpio_set_name(ti_gpio_ctrl* self, uintptr_t regs) {
  static const char digits[] = "0123456789abcdef";
  char* p = self->name;
  size_t i;

  memcpy(p, "ti-gpio@", 8);
  p += 8;

  for (i = 8; i > 0; --i) {
    *p++ = digits[(regs >> ((i - 1) * 4)) & 0xfu];
  }

  *p = '\0';
}

/*
 * The four detect registers are the whole of the trigger: which of them
 * carries a pin is what the pin triggers on, and a pin in none of them
 * triggers on nothing.
 */
static void ti_gpio_set_trigger(ti_gpio_ctrl* self, uint32_t mask,
                                rtems_gpio_trigger trigger) {
  uint32_t rise = TI_GPIO_REG(self, TI_GPIO_RISINGDETECT) & ~mask;
  uint32_t fall = TI_GPIO_REG(self, TI_GPIO_FALLINGDETECT) & ~mask;
  uint32_t low = TI_GPIO_REG(self, TI_GPIO_LEVELDETECT0) & ~mask;
  uint32_t high = TI_GPIO_REG(self, TI_GPIO_LEVELDETECT1) & ~mask;

  switch (trigger) {
  case RTEMS_GPIO_TRIGGER_EDGE_RISING:
    rise |= mask;
    break;
  case RTEMS_GPIO_TRIGGER_EDGE_FALLING:
    fall |= mask;
    break;
  case RTEMS_GPIO_TRIGGER_EDGE_BOTH:
    rise |= mask;
    fall |= mask;
    break;
  case RTEMS_GPIO_TRIGGER_LEVEL_HIGH:
    high |= mask;
    break;
  case RTEMS_GPIO_TRIGGER_LEVEL_LOW:
    low |= mask;
    break;
  case RTEMS_GPIO_TRIGGER_NONE:
    break;
  }

  TI_GPIO_REG(self, TI_GPIO_RISINGDETECT) = rise;
  TI_GPIO_REG(self, TI_GPIO_FALLINGDETECT) = fall;
  TI_GPIO_REG(self, TI_GPIO_LEVELDETECT0) = low;
  TI_GPIO_REG(self, TI_GPIO_LEVELDETECT1) = high;
}

static rtems_gpio_trigger ti_gpio_get_trigger(const ti_gpio_ctrl* self,
                                              uint32_t mask) {
  bool rise = (TI_GPIO_REG(self, TI_GPIO_RISINGDETECT) & mask) != 0;
  bool fall = (TI_GPIO_REG(self, TI_GPIO_FALLINGDETECT) & mask) != 0;

  if (rise && fall) {
    return RTEMS_GPIO_TRIGGER_EDGE_BOTH;
  }

  if (rise) {
    return RTEMS_GPIO_TRIGGER_EDGE_RISING;
  }

  if (fall) {
    return RTEMS_GPIO_TRIGGER_EDGE_FALLING;
  }

  if ((TI_GPIO_REG(self, TI_GPIO_LEVELDETECT1) & mask) != 0) {
    return RTEMS_GPIO_TRIGGER_LEVEL_HIGH;
  }

  if ((TI_GPIO_REG(self, TI_GPIO_LEVELDETECT0) & mask) != 0) {
    return RTEMS_GPIO_TRIGGER_LEVEL_LOW;
  }

  return RTEMS_GPIO_TRIGGER_NONE;
}

/*
 * SETDATAOUT and CLEARDATAOUT act only on the bits written as one, so the
 * data path is never a read modify write and an interrupt handler driving
 * another pin of the same block cannot lose it.
 */
static void ti_gpio_write(ti_gpio_ctrl* self, uint32_t mask, uint32_t value) {
  uint32_t set = value & mask;
  uint32_t clear = ~value & mask;

  if (clear != 0) {
    TI_GPIO_REG(self, TI_GPIO_CLEARDATAOUT) = clear;
  }

  if (set != 0) {
    TI_GPIO_REG(self, TI_GPIO_SETDATAOUT) = set;
  }
}

static int ti_gpio_pin_get_info(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                                rtems_gpio_pin_info* info) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);
  uint32_t mask = 1u << pin;

  info->kind = RTEMS_GPIO_PIN_PHYSICAL;
  info->capabilities = TI_GPIO_CAPS;
  info->flags = RTEMS_GPIO_PIN_AVAILABLE;

  if ((self->reserved & mask) != 0) {
    info->flags |= RTEMS_GPIO_PIN_RESERVED;
  }

  if ((self->strapping & mask) != 0) {
    info->flags |= RTEMS_GPIO_PIN_STRAPPING;
  }

  if ((self->no_pad & mask) != 0) {
    info->flags |= RTEMS_GPIO_PIN_NO_PAD;
  }

  if ((self->in_use & mask) != 0) {
    info->flags |= RTEMS_GPIO_PIN_IN_USE;
  }

  if (self->pins[pin].label != NULL) {
    strncpy(info->name, self->pins[pin].label, sizeof(info->name) - 1);
  }

  if (self->pins[pin].owner != NULL) {
    strncpy(info->owner, self->pins[pin].owner, sizeof(info->owner) - 1);
  }

  return 0;
}

static int ti_gpio_pin_configure(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                                 rtems_gpio_config* config) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);
  uint32_t mask = 1u << pin;

  /*
   * Off before anything moves, so a stale edge cannot fire mid-change.
   * The manual's five cycle settling time after a detect register changes
   * is covered by the gap to the separate call that enables the interrupt.
   */
  TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_CLR_0) = mask;
  ti_gpio_set_trigger(self, mask, config->trigger);
  TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_0) = mask;

  if (config->direction == RTEMS_GPIO_DIRECTION_OUTPUT) {
    /*
     * The level while the pad is still an input, so enabling the driver
     * does not put the output register's previous contents on the pin for
     * as long as it takes to reach the next write.  config->initial_value
     * is already physical: the generic layer applied the polarity.
     */
    ti_gpio_write(self, mask, config->initial_value != 0 ? mask : 0);

    /* OE is an output disable, so a zero is what drives the pad. */
    TI_GPIO_REG(self, TI_GPIO_OE) &= ~mask;
    self->out_read |= mask;
  } else {
    /*
     * RTEMS_GPIO_DIRECTION_NONE lands here too.  An input is the state
     * that drives nothing, which is the safe reading of a pin a caller
     * took without saying what for.
     */
    TI_GPIO_REG(self, TI_GPIO_OE) |= mask;
    self->out_read &= ~mask;
  }

  if (config->trigger != RTEMS_GPIO_TRIGGER_NONE) {
    self->has_trigger |= mask;
  } else {
    self->has_trigger &= ~mask;
  }

  if ((config->flags & RTEMS_GPIO_FLAG_ACTIVE_LOW) != 0) {
    self->inverted |= mask;
  } else {
    self->inverted &= ~mask;
  }

  self->in_use |= mask;

  return 0;
}

static int ti_gpio_pin_get_config(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                                  rtems_gpio_config* config) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);
  uint32_t mask = 1u << pin;

  if ((TI_GPIO_REG(self, TI_GPIO_OE) & mask) == 0) {
    config->direction = RTEMS_GPIO_DIRECTION_OUTPUT;
    config->initial_value =
        (int)((TI_GPIO_REG(self, TI_GPIO_DATAOUT) >> pin) & 1u);
  } else {
    config->direction = RTEMS_GPIO_DIRECTION_INPUT;
  }

  config->trigger = ti_gpio_get_trigger(self, mask);

  if ((self->inverted & mask) != 0) {
    config->flags |= RTEMS_GPIO_FLAG_ACTIVE_LOW;
  }

  return 0;
}

static int ti_gpio_pin_release(rtems_gpio_drv_ctrl* ctrl, uint32_t pin) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);
  uint32_t mask = 1u << pin;

  TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_CLR_0) = mask;
  TI_GPIO_REG(self, TI_GPIO_OE) |= mask;
  ti_gpio_set_trigger(self, mask, RTEMS_GPIO_TRIGGER_NONE);
  TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_0) = mask;

  self->pins[pin].handler = NULL;
  self->in_use &= ~mask;
  self->out_read &= ~mask;
  self->inverted &= ~mask;
  self->has_trigger &= ~mask;

  return 0;
}

/*
 * DATAIN is the pad and DATAOUT is what the block drives.  They are the
 * same thing for an input and need not be for an output, whose pad may be
 * held somewhere else by whatever it is wired to, so an output reads back
 * the level it was told to drive.
 */
static int ti_gpio_pin_get(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                           int* value) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);
  uint32_t reg =
      (self->out_read & (1u << pin)) != 0 ? TI_GPIO_DATAOUT : TI_GPIO_DATAIN;

  *value = (int)((TI_GPIO_REG(self, reg) >> pin) & 1u);

  return 0;
}

static int ti_gpio_pin_set(rtems_gpio_drv_ctrl* ctrl, uint32_t pin, int value) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);
  uint32_t mask = 1u << pin;

  ti_gpio_write(self, mask, value != 0 ? mask : 0);

  return 0;
}

static int ti_gpio_pin_toggle(rtems_gpio_drv_ctrl* ctrl, uint32_t pin) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);
  uint32_t mask = 1u << pin;

  ti_gpio_write(self, mask, ~TI_GPIO_REG(self, TI_GPIO_DATAOUT) & mask);

  return 0;
}

/*
 * One block is 32 pins, so word 0 of a bulk bitmap is the whole of it and
 * word 1 upwards is storage this controller has nothing to put in.  A
 * caller that allocated no words has asked about no pins.
 */
static int ti_gpio_pin_get_multiple(rtems_gpio_drv_ctrl* ctrl,
                                    const uint32_t* mask, uint32_t* values,
                                    size_t words) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);
  uint32_t want;
  uint32_t level;

  if (words == 0) {
    return 0;
  }

  want = mask[0] & self->pin_mask;
  level = TI_GPIO_REG(self, TI_GPIO_DATAIN) & ~self->out_read;
  level |= TI_GPIO_REG(self, TI_GPIO_DATAOUT) & self->out_read;
  values[0] = level & want;

  return 0;
}

static int ti_gpio_pin_set_multiple(rtems_gpio_drv_ctrl* ctrl,
                                    const uint32_t* mask,
                                    const uint32_t* values, size_t words) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);

  if (words == 0) {
    return 0;
  }

  ti_gpio_write(self, mask[0] & self->pin_mask, values[0]);

  return 0;
}

/*
 * IRQSTATUS_0 already carries only the pins whose interrupt is enabled, so
 * it needs no masking.  The bits are cleared before dispatch, so an edge
 * that arrives while a handler runs is not lost.  A level trigger holds
 * its bit up until the cause is removed and so re-enters its handler; that
 * is what a level trigger is and the header says so.
 */
static void ti_gpio_isr(void* arg) {
  ti_gpio_ctrl* self = arg;
  uint32_t status = TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_0) & self->pin_mask;
  uint32_t pin;

  if (status == 0) {
    return;
  }

  TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_0) = status;

  for (pin = 0; status != 0; ++pin, status >>= 1) {
    if ((status & 1u) == 0) {
      continue;
    }

    if (self->pins[pin].handler != NULL) {
      (*self->pins[pin].handler)(pin, self->pins[pin].arg);
    }
  }
}

static int ti_gpio_pin_irq_enable(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                                  rtems_gpio_irq_handler handler, void* arg) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);
  uint32_t mask = 1u << pin;

  /* No "interrupts" in the device tree means nothing delivers this. */
  if (!self->has_irq) {
    return ENOTSUP;
  }

  if ((self->has_trigger & mask) == 0) {
    return EINVAL;
  }

  self->pins[pin].handler = handler;
  self->pins[pin].arg = arg;

  TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_0) = mask;
  TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_SET_0) = mask;

  return 0;
}

static int ti_gpio_pin_irq_disable(rtems_gpio_drv_ctrl* ctrl, uint32_t pin) {
  ti_gpio_ctrl* self = ti_gpio_downcast(ctrl);

  /* The hardware first, so the handler is not called after it is gone. */
  TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_CLR_0) = 1u << pin;
  self->pins[pin].handler = NULL;

  return 0;
}

static const rtems_gpio_drv_handlers ti_gpio_handlers = {
    .pin_get_info = ti_gpio_pin_get_info,
    .pin_configure = ti_gpio_pin_configure,
    .pin_get_config = ti_gpio_pin_get_config,
    .pin_release = ti_gpio_pin_release,
    .pin_get = ti_gpio_pin_get,
    .pin_set = ti_gpio_pin_set,
    .pin_toggle = ti_gpio_pin_toggle,
    .pin_get_multiple = ti_gpio_pin_get_multiple,
    .pin_set_multiple = ti_gpio_pin_set_multiple,
    .pin_irq_enable = ti_gpio_pin_irq_enable,
    .pin_irq_disable = ti_gpio_pin_irq_disable};

static bool ti_gpio_node_enabled(const void* fdt, int node) {
  int len;
  const char* status = fdt_getprop(fdt, node, "status", &len);

  if (status == NULL || len <= 0) {
    return true;
  }

  return strcmp(status, "okay") == 0 || strcmp(status, "ok") == 0;
}

static uint64_t ti_gpio_read_cells(const fdt32_t* cells, int count) {
  uint64_t value = 0;
  int i;

  for (i = 0; i < count; ++i) {
    value = (value << 32) | fdt32_ld(&cells[i]);
  }

  return value;
}

/*
 * One hop of an address up the tree.  No "ranges" at all means the bus
 * does not map its children's addresses anywhere, an empty one means it
 * maps them unchanged, and otherwise the triplet covering the address
 * gives the offset.  The child half of a triplet is counted in the bus's
 * own "#address-cells" and the parent half in its parent's, which is the
 * only reason both are read.
 */
static int ti_gpio_translate(const void* fdt, int node, int parent,
                             uint64_t* address) {
  int len;
  const fdt32_t* ranges = fdt_getprop(fdt, node, "ranges", &len);
  int child_cells = fdt_address_cells(fdt, node);
  int parent_cells = fdt_address_cells(fdt, parent);
  int size_cells = fdt_size_cells(fdt, node);
  int stride;
  int i;

  if (ranges == NULL) {
    return EINVAL;
  }

  if (len == 0) {
    return 0;
  }

  if (child_cells < 1 || child_cells > 2 || parent_cells < 1 ||
      parent_cells > 2 || size_cells < 1 || size_cells > 2) {
    return EINVAL;
  }

  stride = child_cells + parent_cells + size_cells;

  for (i = 0; (i + stride) * (int)sizeof(*ranges) <= len; i += stride) {
    uint64_t child = ti_gpio_read_cells(&ranges[i], child_cells);
    uint64_t up = ti_gpio_read_cells(&ranges[i + child_cells], parent_cells);
    uint64_t size =
        ti_gpio_read_cells(&ranges[i + child_cells + parent_cells], size_cells);

    if (*address >= child && *address - child < size) {
      *address = up + (*address - child);
      return 0;
    }
  }

  return EINVAL;
}

/*
 * A mainline AM335x tree nests the controller inside a target module whose
 * "ranges" carries the real address, so the node's own "reg" is 0 and the
 * walk to the root is what turns it into a register base.  A tree from
 * before that rewrite puts the address in "reg" and passes through buses
 * that map one to one, which is the same walk.
 */
static int ti_gpio_reg_base(const void* fdt, int node, uintptr_t* base) {
  int parent = fdt_parent_offset(fdt, node);
  int cells;
  int len;
  const fdt32_t* reg;
  uint64_t address;

  if (parent < 0) {
    return EINVAL;
  }

  cells = fdt_address_cells(fdt, parent);
  if (cells < 1 || cells > 2) {
    return EINVAL;
  }

  reg = fdt_getprop(fdt, node, "reg", &len);
  if (reg == NULL || len < cells * (int)sizeof(*reg)) {
    return EINVAL;
  }

  address = ti_gpio_read_cells(reg, cells);

  for (node = parent;; node = parent) {
    int err;

    parent = fdt_parent_offset(fdt, node);
    if (parent < 0) {
      break;
    }

    err = ti_gpio_translate(fdt, node, parent, &address);
    if (err != 0) {
      return err;
    }
  }

  *base = (uintptr_t)address;

  if (address == 0 || (uint64_t)*base != address) {
    return EINVAL;
  }

  return 0;
}

/*
 * The node is itself an interrupt controller for the pins below it, so its
 * own "#interrupt-cells" describes those and not the specifier in its
 * "interrupts".  That one is counted by whatever the node's interrupt
 * parent is, which is the nearest ancestor naming one.
 */
static int ti_gpio_interrupt_cells(const void* fdt, int node) {
  for (; node >= 0; node = fdt_parent_offset(fdt, node)) {
    const fdt32_t* phandle;
    const fdt32_t* cells;
    int parent;

    phandle = fdt_getprop(fdt, node, "interrupt-parent", NULL);
    if (phandle == NULL) {
      continue;
    }

    parent = fdt_node_offset_by_phandle(fdt, fdt32_ld(phandle));
    if (parent < 0) {
      return -1;
    }

    cells = fdt_getprop(fdt, parent, "#interrupt-cells", NULL);
    if (cells == NULL) {
      return -1;
    }

    return (int)fdt32_ld(cells);
  }

  return -1;
}

static bool ti_gpio_irq_vector(const void* fdt, int node, uint32_t* vector) {
  int cells = ti_gpio_interrupt_cells(fdt, node);
  int len;
  const fdt32_t* interrupts = fdt_getprop(fdt, node, "interrupts", &len);

  if (interrupts == NULL || cells < 1 ||
      len < cells * (int)sizeof(*interrupts)) {
    return false;
  }

  if (cells == 1) {
    *vector = fdt32_ld(&interrupts[0]);
    return true;
  }

  if (cells == 3) {
    /* A GIC specifier: 0 is an SPI, which starts at 32, and 1 a PPI, at 16. */
    *vector =
        (fdt32_ld(&interrupts[0]) == 0 ? 32u : 16u) + fdt32_ld(&interrupts[1]);
    return true;
  }

  return false;
}

/*
 * The child nodes annotate pins that already exist, so one naming a pin
 * the controller does not have is a mistake in the tree rather than
 * something to skip quietly: a board that reserved pin 40 believes pin 40
 * is safe.
 */
static int ti_gpio_read_pin_nodes(ti_gpio_ctrl* self, const void* fdt,
                                  int node) {
  int child;

  for (child = fdt_first_subnode(fdt, node); child >= 0;
       child = fdt_next_subnode(fdt, child)) {
    const fdt32_t* number;
    uint32_t mask;
    uint32_t pin;
    int len;

    number = fdt_getprop(fdt, child, "rtems,pin", &len);
    if (number == NULL || len < (int)sizeof(*number)) {
      continue;
    }

    pin = fdt32_ld(number);
    if (pin >= self->base.pin_count) {
      return EINVAL;
    }

    mask = 1u << pin;

    self->pins[pin].label = fdt_getprop(fdt, child, "rtems,label", NULL);
    self->pins[pin].owner = fdt_getprop(fdt, child, "rtems,owner", NULL);

    if (fdt_getprop(fdt, child, "rtems,reserved", NULL) != NULL) {
      self->reserved |= mask;
    }

    if (fdt_getprop(fdt, child, "rtems,strapping", NULL) != NULL) {
      self->strapping |= mask;
    }

    if (fdt_getprop(fdt, child, "rtems,no-pad", NULL) != NULL) {
      self->no_pad |= mask;
    }
  }

  return 0;
}

static int ti_gpio_find(const void* fdt, int node) {
  for (;;) {
    node = fdt_node_offset_by_compatible(fdt, node, TI_GPIO_COMPATIBLE);
    if (node < 0) {
      return -1;
    }

    if (ti_gpio_node_enabled(fdt, node)) {
      return node;
    }
  }
}

static int ti_gpio_setup(ti_gpio_ctrl* self, const void* fdt, int node,
                         uintptr_t regs) {
  const fdt32_t* ngpios;
  uint32_t pins = TI_GPIO_PIN_COUNT_MAX;
  uint32_t vector = 0;
  int len;
  int err;

  ngpios = fdt_getprop(fdt, node, "ngpios", &len);
  if (ngpios != NULL && len >= (int)sizeof(*ngpios)) {
    pins = fdt32_ld(ngpios);

    if (pins == 0 || pins > TI_GPIO_PIN_COUNT_MAX) {
      return EINVAL;
    }
  }

  memset(self, 0, sizeof(*self));
  self->regs = regs;
  self->pin_mask =
      pins >= TI_GPIO_PIN_COUNT_MAX ? 0xffffffffu : (1u << pins) - 1u;
  ti_gpio_set_name(self, regs);

  self->base.handlers = &ti_gpio_handlers;
  self->base.pin_count = pins;
  self->base.name = self->name;

  /* Memory mapped registers, so nothing here waits and a caller may use
   * this controller from interrupt context. */
  self->base.can_block = false;
  self->base.active_low = self->active_low;
  self->base.scratch = self->scratch;

  err = ti_gpio_read_pin_nodes(self, fdt, node);
  if (err != 0) {
    return err;
  }

  err = rtems_gpio_drv_ctrl_init(&self->base);
  if (err != 0) {
    return err;
  }

  /*
   * A boot loader may have left the block in reset, and nothing works
   * until it is not.  What it may also have left is a pin driving
   * something the board needs, so OE and DATAOUT are not touched here and
   * only the interrupt state, which nothing can yet be waiting on, is
   * cleared.
   */
  TI_GPIO_REG(self, TI_GPIO_CTRL) &= ~TI_GPIO_CTRL_DISABLEMODULE;
  TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_CLR_0) = 0xffffffffu;
  ti_gpio_set_trigger(self, 0xffffffffu, RTEMS_GPIO_TRIGGER_NONE);
  TI_GPIO_REG(self, TI_GPIO_IRQSTATUS_0) = 0xffffffffu;

  if (ti_gpio_irq_vector(fdt, node, &vector)) {
    rtems_status_code sc;

    sc = rtems_interrupt_handler_install(
        vector, self->name, RTEMS_INTERRUPT_SHARED, ti_gpio_isr, self);
    if (sc != RTEMS_SUCCESSFUL) {
      return EIO;
    }

    self->vector = vector;
    self->has_irq = true;
  }

  return 0;
}

int ti_gpio_register(const void* fdt, int node, const char* path) {
  ti_gpio_ctrl* self;
  uintptr_t regs;
  size_t i;
  int err;

  if (fdt == NULL || path == NULL) {
    return EINVAL;
  }

  if (ti_gpio_instance_count >= TI_GPIO_CTRL_MAX) {
    return EBUSY;
  }

  if (fdt_check_header(fdt) != 0) {
    return EINVAL;
  }

  if (node < 0) {
    node = ti_gpio_find(fdt, -1);
    if (node < 0) {
      return EINVAL;
    }
  }

  if (fdt_node_check_compatible(fdt, node, TI_GPIO_COMPATIBLE) != 0) {
    return EINVAL;
  }

  err = ti_gpio_reg_base(fdt, node, &regs);
  if (err != 0) {
    return err;
  }

  /* Two controllers over one register block would each believe they own
   * it, and the second would clear the first's interrupt state. */
  for (i = 0; i < ti_gpio_instance_count; ++i) {
    if (ti_gpio_instances[i].regs == regs) {
      return EBUSY;
    }
  }

  self = &ti_gpio_instances[ti_gpio_instance_count];

  err = ti_gpio_setup(self, fdt, node, regs);
  if (err != 0) {
    return err;
  }

  if (rtems_gpio_drv_ctrl_register(&self->base, path) != 0) {
    err = errno;

    if (self->has_irq) {
      (void)rtems_interrupt_handler_remove(self->vector, ti_gpio_isr, self);
      self->has_irq = false;
    }

    return err;
  }

  ++ti_gpio_instance_count;

  return 0;
}

/*
 * The device tree numbers nothing: a mainline AM335x tree has no aliases
 * for these nodes and, since the rewrite that nested each one inside a
 * target module, their node names no longer carry an address either.  What
 * is left that a board cannot get wrong is the register base, and on this
 * part its order is the manual's order, so ascending base gives GPIO0
 * through GPIO3 as /dev/gpio0 through /dev/gpio3.
 */
int ti_gpio_register_all(const void* fdt, const char* prefix) {
  int nodes[TI_GPIO_CTRL_MAX];
  uintptr_t bases[TI_GPIO_CTRL_MAX];
  char path[RTEMS_GPIO_NAME_MAX];
  size_t length;
  size_t found = 0;
  size_t i;
  int node = -1;
  int err;

  if (fdt == NULL || prefix == NULL) {
    return EINVAL;
  }

  if (fdt_check_header(fdt) != 0) {
    return EINVAL;
  }

  length = strlen(prefix);
  if (length + 2 > sizeof(path)) {
    return EINVAL;
  }

  for (;;) {
    node = ti_gpio_find(fdt, node);
    if (node < 0) {
      break;
    }

    if (found == RTEMS_ARRAY_SIZE(nodes)) {
      return EBUSY;
    }

    err = ti_gpio_reg_base(fdt, node, &bases[found]);
    if (err != 0) {
      return err;
    }

    nodes[found] = node;
    ++found;
  }

  if (found == 0) {
    return EINVAL;
  }

  for (i = 0; i + 1 < found; ++i) {
    size_t low = i;
    size_t j;

    for (j = i + 1; j < found; ++j) {
      if (bases[j] < bases[low]) {
        low = j;
      }
    }

    if (low != i) {
      uintptr_t base = bases[i];
      int swap = nodes[i];

      bases[i] = bases[low];
      bases[low] = base;
      nodes[i] = nodes[low];
      nodes[low] = swap;
    }
  }

  memcpy(path, prefix, length);
  path[length + 1] = '\0';

  for (i = 0; i < found; ++i) {
    path[length] = (char)('0' + (int)i);

    err = ti_gpio_register(fdt, nodes[i], path);
    if (err != 0) {
      return err;
    }
  }

  return 0;
}
