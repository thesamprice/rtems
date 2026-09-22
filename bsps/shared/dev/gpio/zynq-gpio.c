/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSDeviceZynqGPIO
 *
 * @brief This source file contains the implementation of the Xilinx Zynq
 *   and Zynq UltraScale+ MPSoC GPIO controller.
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

#include <dev/gpio/zynq-gpio.h>

#include <rtems/irq-extension.h>
#include <rtems/score/basedefs.h>

#include <errno.h>
#include <libfdt.h>
#include <string.h>

/*
 * The Zynq-7000 and the ZynqMP have the same GPIO block and differ only in
 * how many banks it has and how wide each one is.  So there is one driver
 * and no conditional compilation: which geometry applies is decided at run
 * time from the compatible string in the device tree, which is also the
 * only place the register base and the interrupt vector come from.
 */

/* Six banks is the ZynqMP, which is the wider of the two parts. */
#define ZYNQ_GPIO_BANK_MAX 6

#define ZYNQ_GPIO_WORDS RTEMS_GPIO_BITMAP_WORDS(ZYNQ_GPIO_PIN_COUNT_MAX)

/*
 * Register offsets.  The data registers are packed per bank at the bottom
 * of the block and the control registers are 0x40 apart from 0x200 up.
 */
#define ZYNQ_GPIO_DATA_LSW(b) (0x000u + 8u * (b))
#define ZYNQ_GPIO_DATA_MSW(b) (0x004u + 8u * (b))
#define ZYNQ_GPIO_DATA(b) (0x040u + 4u * (b))
#define ZYNQ_GPIO_DATA_RO(b) (0x060u + 4u * (b))
#define ZYNQ_GPIO_DIRM(b) (0x204u + 0x40u * (b))
#define ZYNQ_GPIO_OEN(b) (0x208u + 0x40u * (b))
#define ZYNQ_GPIO_INT_MASK(b) (0x20cu + 0x40u * (b))
#define ZYNQ_GPIO_INT_EN(b) (0x210u + 0x40u * (b))
#define ZYNQ_GPIO_INT_DIS(b) (0x214u + 0x40u * (b))
#define ZYNQ_GPIO_INT_STAT(b) (0x218u + 0x40u * (b))
#define ZYNQ_GPIO_INT_TYPE(b) (0x21cu + 0x40u * (b))
#define ZYNQ_GPIO_INT_POL(b) (0x220u + 0x40u * (b))
#define ZYNQ_GPIO_INT_ANY(b) (0x224u + 0x40u * (b))

#define ZYNQ_GPIO_REG(self, off) (*(volatile uint32_t*)((self)->regs + (off)))

/*
 * What the block can do for every pin of both parts.  There is no per-pin
 * variation here beyond MIO against EMIO below; what differs between pins
 * is what the board has spoken for, and that comes from the device tree.
 */
#define ZYNQ_GPIO_CAPS_COMMON                                                  \
  (RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_OUTPUT | RTEMS_GPIO_CAP_EDGE_RISING | \
   RTEMS_GPIO_CAP_EDGE_FALLING | RTEMS_GPIO_CAP_EDGE_BOTH |                    \
   RTEMS_GPIO_CAP_LEVEL_HIGH | RTEMS_GPIO_CAP_LEVEL_LOW)

/* An MIO pad reads back what the pin is at, through DATA_x_RO. */
#define ZYNQ_GPIO_CAPS_MIO                                                     \
  (ZYNQ_GPIO_CAPS_COMMON | RTEMS_GPIO_CAP_OUTPUT_READBACK)

/*
 * An EMIO line does not.  Its input is whatever the PL drives back into the
 * controller, which is a different signal from the one the controller
 * drives out, so a read of an output EMIO line comes from DATA_x instead.
 */
#define ZYNQ_GPIO_CAPS_EMIO ZYNQ_GPIO_CAPS_COMMON

/*
 * The pull resistors, the drive strength and the slew rate are in the SLCR
 * on the Zynq-7000 and in the IOU on the ZynqMP, not in this block, and
 * neither part debounces a pin or wakes from one here.  Claiming any of
 * them would make the generic layer accept a configuration that nothing
 * below it would then apply.
 */
#define ZYNQ_GPIO_CAPS_ELSEWHERE                                               \
  (RTEMS_GPIO_CAP_PULL_UP | RTEMS_GPIO_CAP_PULL_DOWN |                         \
   RTEMS_GPIO_CAP_OPEN_DRAIN | RTEMS_GPIO_CAP_OPEN_SOURCE |                    \
   RTEMS_GPIO_CAP_DRIVE_STRENGTH | RTEMS_GPIO_CAP_DEBOUNCE |                   \
   RTEMS_GPIO_CAP_WAKEUP)

RTEMS_STATIC_ASSERT((ZYNQ_GPIO_CAPS_MIO & ZYNQ_GPIO_CAPS_ELSEWHERE) == 0,
                    zynq_gpio_claims_only_what_this_block_has);

RTEMS_STATIC_ASSERT((ZYNQ_GPIO_CAPS_EMIO & ~ZYNQ_GPIO_CAPS_MIO) == 0,
                    zynq_gpio_emio_does_no_more_than_mio);

RTEMS_STATIC_ASSERT(ZYNQ_GPIO_PIN_COUNT_MAX <=
                        ZYNQ_GPIO_BANK_MAX * RTEMS_GPIO_BITMAP_WORD_BITS,
                    zynq_gpio_banks_cover_every_pin);

/* What one part's GPIO block looks like. */
typedef struct {
  /**
   * @brief This member contains the compatible string of the part.
   */
  const char* compatible;

  /**
   * @brief This member contains what a controller of this part calls
   *   itself.
   */
  const char* name;

  /**
   * @brief This member contains the number of banks the part has.
   */
  uint8_t bank_count;

  /**
   * @brief This member contains how many of the first banks are MIO, the
   *   rest being EMIO.
   */
  uint8_t mio_banks;

  /**
   * @brief This member contains the width in pins of each bank.
   */
  uint8_t bank_width[ZYNQ_GPIO_BANK_MAX];
} zynq_gpio_variant;

/*
 * Zynq-7000: 54 MIO in two banks, the second of them 22 wide, then 64 EMIO.
 * ZynqMP: 78 MIO in three banks of 26, then 96 EMIO.  A bank narrower than
 * a word is why this driver cannot treat a bulk bitmap word as a bank.
 */
static const zynq_gpio_variant zynq_gpio_variants[] = {
    {ZYNQ_GPIO_COMPATIBLE, "zynq-gpio", 4, 2, {32, 22, 32, 32}},
    {ZYNQMP_GPIO_COMPATIBLE, "zynqmp-gpio", 6, 3, {26, 26, 26, 32, 32, 32}}};

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
} zynq_gpio_pin;

/*
 * The controller.  Everything per bank is a mask of bank bits rather than a
 * pin bitmap, because every register in the block is one bank wide and the
 * masks then go straight into them.
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
   * @brief This member contains the number of banks in use.
   */
  uint32_t bank_count;

  /**
   * @brief This member contains how many of the first banks are MIO.
   */
  uint32_t mio_banks;

  /**
   * @brief This member contains the first logical pin of each bank.
   */
  uint32_t bank_first[ZYNQ_GPIO_BANK_MAX];

  /**
   * @brief This member contains the usable bits of each bank.
   */
  uint32_t bank_mask[ZYNQ_GPIO_BANK_MAX];

  /**
   * @brief This member contains one bit per configured pin.
   */
  uint32_t in_use[ZYNQ_GPIO_BANK_MAX];

  /**
   * @brief This member contains the pins whose level must be read from the
   *   output register rather than from the pin.
   */
  uint32_t out_read[ZYNQ_GPIO_BANK_MAX];

  /**
   * @brief This member contains the pins configured active low.
   */
  uint32_t inverted[ZYNQ_GPIO_BANK_MAX];

  /**
   * @brief This member contains the pins configured with a trigger.
   */
  uint32_t has_trigger[ZYNQ_GPIO_BANK_MAX];

  /**
   * @brief This member contains the pins the device tree reserves.
   */
  uint32_t reserved[ZYNQ_GPIO_BANK_MAX];

  /**
   * @brief This member contains the pins the device tree marks strapping.
   */
  uint32_t strapping[ZYNQ_GPIO_BANK_MAX];

  /**
   * @brief This member contains the pins the device tree says reach no pad.
   */
  uint32_t no_pad[ZYNQ_GPIO_BANK_MAX];

  /**
   * @brief This member contains the polarity bitmap the generic layer
   *   maintains.
   */
  uint32_t active_low[ZYNQ_GPIO_WORDS];

  /**
   * @brief This member contains the scratch bitmap the generic layer needs
   *   for a bulk write.
   */
  uint32_t scratch[ZYNQ_GPIO_WORDS];

  /**
   * @brief This member is true, if an interrupt handler was installed,
   *   otherwise false.
   */
  bool has_irq;

  /**
   * @brief This member contains what the device tree said about each pin.
   */
  zynq_gpio_pin pins[ZYNQ_GPIO_PIN_COUNT_MAX];
} zynq_gpio_ctrl;

static zynq_gpio_ctrl zynq_gpio_instance;

static bool zynq_gpio_registered;

static zynq_gpio_ctrl* zynq_gpio_downcast(rtems_gpio_drv_ctrl* ctrl) {
  return RTEMS_CONTAINER_OF(ctrl, zynq_gpio_ctrl, base);
}

static uint32_t zynq_gpio_width_mask(uint32_t width) {
  return width >= RTEMS_GPIO_BITMAP_WORD_BITS ? 0xffffffffu
                                              : (1u << width) - 1u;
}

/*
 * The generic layer has already checked that the pin is one this controller
 * publishes, so the search cannot run off the bottom: bank 0 starts at pin
 * 0.
 */
static void zynq_gpio_locate(const zynq_gpio_ctrl* self, uint32_t pin,
                             uint32_t* bank, uint32_t* bit) {
  uint32_t b = self->bank_count - 1;

  while (pin < self->bank_first[b]) {
    --b;
  }

  *bank = b;
  *bit = pin - self->bank_first[b];
}

/*
 * MASK_DATA is why a write here is never a read modify write: the upper
 * half of each register says which of the sixteen data bits below it to
 * leave alone, so a write cannot disturb a pin it was not asked about, and
 * an interrupt handler driving another pin in the same bank cannot lose it.
 */
static void zynq_gpio_bank_write(zynq_gpio_ctrl* self, uint32_t bank,
                                 uint32_t mask, uint32_t value) {
  if ((mask & 0xffffu) != 0) {
    ZYNQ_GPIO_REG(self, ZYNQ_GPIO_DATA_LSW(bank)) =
        ((~mask & 0xffffu) << 16) | (value & 0xffffu);
  }

  if ((mask >> 16) != 0) {
    ZYNQ_GPIO_REG(self, ZYNQ_GPIO_DATA_MSW(bank)) =
        ((~mask >> 16) << 16) | ((value >> 16) & 0xffffu);
  }
}

/*
 * A bulk bitmap is indexed by pin and a bank of fewer than 32 pins puts the
 * next bank part way into a word, so a bank straddles two words and these
 * two have to shift across the boundary.  The caller's word count is a hard
 * limit in both: a word past it is storage that was never allocated.
 */
static uint32_t zynq_gpio_extract(const uint32_t* map, uint32_t first,
                                  uint32_t mask, size_t words) {
  size_t word = first / RTEMS_GPIO_BITMAP_WORD_BITS;
  uint32_t shift = first % RTEMS_GPIO_BITMAP_WORD_BITS;
  uint32_t value = 0;

  if (word < words) {
    value = map[word] >> shift;
  }

  if (shift != 0 && word + 1 < words) {
    value |= map[word + 1] << (RTEMS_GPIO_BITMAP_WORD_BITS - shift);
  }

  return value & mask;
}

static void zynq_gpio_insert(uint32_t* map, uint32_t first, uint32_t mask,
                             size_t words, uint32_t value) {
  size_t word = first / RTEMS_GPIO_BITMAP_WORD_BITS;
  uint32_t shift = first % RTEMS_GPIO_BITMAP_WORD_BITS;

  value &= mask;

  if (word < words) {
    map[word] = (map[word] & ~(mask << shift)) | (value << shift);
  }

  if (shift != 0 && word + 1 < words) {
    uint32_t back = RTEMS_GPIO_BITMAP_WORD_BITS - shift;

    map[word + 1] = (map[word + 1] & ~(mask >> back)) | (value >> back);
  }
}

static void zynq_gpio_set_trigger(zynq_gpio_ctrl* self, uint32_t bank,
                                  uint32_t mask, rtems_gpio_trigger trigger) {
  uint32_t type = ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_TYPE(bank)) & ~mask;
  uint32_t pol = ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_POL(bank)) & ~mask;
  uint32_t any = ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_ANY(bank)) & ~mask;

  switch (trigger) {
  case RTEMS_GPIO_TRIGGER_EDGE_RISING:
    type |= mask;
    pol |= mask;
    break;
  case RTEMS_GPIO_TRIGGER_EDGE_FALLING:
    type |= mask;
    break;
  case RTEMS_GPIO_TRIGGER_EDGE_BOTH:
    type |= mask;
    any |= mask;
    break;
  case RTEMS_GPIO_TRIGGER_LEVEL_HIGH:
    pol |= mask;
    break;
  case RTEMS_GPIO_TRIGGER_LEVEL_LOW:
  case RTEMS_GPIO_TRIGGER_NONE:
    break;
  }

  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_TYPE(bank)) = type;
  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_POL(bank)) = pol;
  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_ANY(bank)) = any;
}

static rtems_gpio_trigger zynq_gpio_get_trigger(const zynq_gpio_ctrl* self,
                                                uint32_t bank, uint32_t mask) {
  bool edge;
  bool high;

  /*
   * A pin asked for no trigger leaves the registers reading level low, so
   * the registers alone cannot tell the two apart and the bitmap does.
   */
  if ((self->has_trigger[bank] & mask) == 0) {
    return RTEMS_GPIO_TRIGGER_NONE;
  }

  edge = (ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_TYPE(bank)) & mask) != 0;
  high = (ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_POL(bank)) & mask) != 0;

  if (!edge) {
    return high ? RTEMS_GPIO_TRIGGER_LEVEL_HIGH : RTEMS_GPIO_TRIGGER_LEVEL_LOW;
  }

  if ((ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_ANY(bank)) & mask) != 0) {
    return RTEMS_GPIO_TRIGGER_EDGE_BOTH;
  }

  return high ? RTEMS_GPIO_TRIGGER_EDGE_RISING
              : RTEMS_GPIO_TRIGGER_EDGE_FALLING;
}

static int zynq_gpio_pin_get_info(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                                  rtems_gpio_pin_info* info) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;
  uint32_t bit;
  uint32_t mask;

  zynq_gpio_locate(self, pin, &bank, &bit);
  mask = 1u << bit;

  /*
   * An EMIO line is not a pad on the package, it is a line into the PL,
   * which is the case the API calls virtual.  Saying so is what tells a
   * caller that finding the pin on the board is a question for whoever
   * built the bitstream.
   */
  if (bank < self->mio_banks) {
    info->kind = RTEMS_GPIO_PIN_PHYSICAL;
    info->capabilities = ZYNQ_GPIO_CAPS_MIO;
  } else {
    info->kind = RTEMS_GPIO_PIN_VIRTUAL;
    info->capabilities = ZYNQ_GPIO_CAPS_EMIO;
  }

  info->flags = RTEMS_GPIO_PIN_AVAILABLE;

  if ((self->reserved[bank] & mask) != 0) {
    info->flags |= RTEMS_GPIO_PIN_RESERVED;
  }

  if ((self->strapping[bank] & mask) != 0) {
    info->flags |= RTEMS_GPIO_PIN_STRAPPING;
  }

  if ((self->no_pad[bank] & mask) != 0) {
    info->flags |= RTEMS_GPIO_PIN_NO_PAD;
  }

  if ((self->in_use[bank] & mask) != 0) {
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

static int zynq_gpio_pin_configure(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                                   rtems_gpio_config* config) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;
  uint32_t bit;
  uint32_t mask;

  zynq_gpio_locate(self, pin, &bank, &bit);
  mask = 1u << bit;

  /* Off before anything moves, so a stale edge cannot fire mid-change. */
  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_DIS(bank)) = mask;
  zynq_gpio_set_trigger(self, bank, mask, config->trigger);
  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_STAT(bank)) = mask;

  if (config->direction == RTEMS_GPIO_DIRECTION_OUTPUT) {
    /*
     * The level while the pad is still an input, so enabling the driver
     * does not put the output register's previous contents on the pin for
     * as long as it takes to reach the next write.  config->initial_value
     * is already physical: the generic layer applied the polarity.
     */
    zynq_gpio_bank_write(self, bank, mask,
                         config->initial_value != 0 ? mask : 0);
    ZYNQ_GPIO_REG(self, ZYNQ_GPIO_DIRM(bank)) |= mask;
    ZYNQ_GPIO_REG(self, ZYNQ_GPIO_OEN(bank)) |= mask;

    if (bank >= self->mio_banks) {
      self->out_read[bank] |= mask;
    }
  } else {
    /*
     * RTEMS_GPIO_DIRECTION_NONE lands here too.  An input is the state
     * that drives nothing, which is the safe reading of a pin a caller
     * took without saying what for.
     */
    ZYNQ_GPIO_REG(self, ZYNQ_GPIO_OEN(bank)) &= ~mask;
    ZYNQ_GPIO_REG(self, ZYNQ_GPIO_DIRM(bank)) &= ~mask;
    self->out_read[bank] &= ~mask;
  }

  if (config->trigger != RTEMS_GPIO_TRIGGER_NONE) {
    self->has_trigger[bank] |= mask;
  } else {
    self->has_trigger[bank] &= ~mask;
  }

  if ((config->flags & RTEMS_GPIO_FLAG_ACTIVE_LOW) != 0) {
    self->inverted[bank] |= mask;
  } else {
    self->inverted[bank] &= ~mask;
  }

  self->in_use[bank] |= mask;

  return 0;
}

static int zynq_gpio_pin_get_config(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                                    rtems_gpio_config* config) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;
  uint32_t bit;
  uint32_t mask;

  zynq_gpio_locate(self, pin, &bank, &bit);
  mask = 1u << bit;

  if ((ZYNQ_GPIO_REG(self, ZYNQ_GPIO_DIRM(bank)) & mask) != 0) {
    config->direction = RTEMS_GPIO_DIRECTION_OUTPUT;
    config->initial_value =
        (int)((ZYNQ_GPIO_REG(self, ZYNQ_GPIO_DATA(bank)) >> bit) & 1u);
  } else {
    config->direction = RTEMS_GPIO_DIRECTION_INPUT;
  }

  config->trigger = zynq_gpio_get_trigger(self, bank, mask);

  if ((self->inverted[bank] & mask) != 0) {
    config->flags |= RTEMS_GPIO_FLAG_ACTIVE_LOW;
  }

  return 0;
}

static int zynq_gpio_pin_release(rtems_gpio_drv_ctrl* ctrl, uint32_t pin) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;
  uint32_t bit;
  uint32_t mask;

  zynq_gpio_locate(self, pin, &bank, &bit);
  mask = 1u << bit;

  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_DIS(bank)) = mask;
  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_OEN(bank)) &= ~mask;
  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_DIRM(bank)) &= ~mask;
  zynq_gpio_set_trigger(self, bank, mask, RTEMS_GPIO_TRIGGER_NONE);
  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_STAT(bank)) = mask;

  self->pins[pin].handler = NULL;
  self->in_use[bank] &= ~mask;
  self->out_read[bank] &= ~mask;
  self->inverted[bank] &= ~mask;
  self->has_trigger[bank] &= ~mask;

  return 0;
}

static int zynq_gpio_pin_get(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                             int* value) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;
  uint32_t bit;
  uint32_t reg;

  zynq_gpio_locate(self, pin, &bank, &bit);

  reg = (self->out_read[bank] & (1u << bit)) != 0 ? ZYNQ_GPIO_DATA(bank)
                                                  : ZYNQ_GPIO_DATA_RO(bank);

  *value = (int)((ZYNQ_GPIO_REG(self, reg) >> bit) & 1u);

  return 0;
}

static int zynq_gpio_pin_set(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                             int value) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;
  uint32_t bit;
  uint32_t mask;

  zynq_gpio_locate(self, pin, &bank, &bit);
  mask = 1u << bit;

  zynq_gpio_bank_write(self, bank, mask, value != 0 ? mask : 0);

  return 0;
}

static int zynq_gpio_pin_toggle(rtems_gpio_drv_ctrl* ctrl, uint32_t pin) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;
  uint32_t bit;
  uint32_t mask;
  uint32_t now;

  zynq_gpio_locate(self, pin, &bank, &bit);
  mask = 1u << bit;

  /* The driven level, not the pin: toggling an output inverts what it
   * drives, whatever the pad may be held at. */
  now = ZYNQ_GPIO_REG(self, ZYNQ_GPIO_DATA(bank));

  zynq_gpio_bank_write(self, bank, mask, ~now & mask);

  return 0;
}

static int zynq_gpio_pin_get_multiple(rtems_gpio_drv_ctrl* ctrl,
                                      const uint32_t* mask, uint32_t* values,
                                      size_t words) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;

  for (bank = 0; bank < self->bank_count; ++bank) {
    uint32_t want;
    uint32_t level;

    want = zynq_gpio_extract(mask, self->bank_first[bank],
                             self->bank_mask[bank], words);
    if (want == 0) {
      continue;
    }

    level =
        ZYNQ_GPIO_REG(self, ZYNQ_GPIO_DATA_RO(bank)) & ~self->out_read[bank];
    level |= ZYNQ_GPIO_REG(self, ZYNQ_GPIO_DATA(bank)) & self->out_read[bank];

    zynq_gpio_insert(values, self->bank_first[bank], want, words, level);
  }

  return 0;
}

static int zynq_gpio_pin_set_multiple(rtems_gpio_drv_ctrl* ctrl,
                                      const uint32_t* mask,
                                      const uint32_t* values, size_t words) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;

  for (bank = 0; bank < self->bank_count; ++bank) {
    uint32_t first = self->bank_first[bank];
    uint32_t want;

    want = zynq_gpio_extract(mask, first, self->bank_mask[bank], words);
    if (want == 0) {
      continue;
    }

    zynq_gpio_bank_write(
        self, bank, want,
        zynq_gpio_extract(values, first, self->bank_mask[bank], words));
  }

  return 0;
}

/*
 * One interrupt for the whole block, so the handler walks the banks and
 * calls whoever asked.  The status bits are cleared before dispatch, so an
 * edge that arrives while a handler runs is not lost.
 */
static void zynq_gpio_isr(void* arg) {
  zynq_gpio_ctrl* self = arg;
  uint32_t bank;

  for (bank = 0; bank < self->bank_count; ++bank) {
    uint32_t first = self->bank_first[bank];
    uint32_t status;
    uint32_t bit;

    status = ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_STAT(bank));
    status &= ~ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_MASK(bank));
    status &= self->bank_mask[bank];

    if (status == 0) {
      continue;
    }

    ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_STAT(bank)) = status;

    for (bit = 0; status != 0; ++bit, status >>= 1) {
      uint32_t pin;

      if ((status & 1u) == 0) {
        continue;
      }

      pin = first + bit;

      if (self->pins[pin].handler != NULL) {
        (*self->pins[pin].handler)(pin, self->pins[pin].arg);
      }
    }
  }
}

static int zynq_gpio_pin_irq_enable(rtems_gpio_drv_ctrl* ctrl, uint32_t pin,
                                    rtems_gpio_irq_handler handler, void* arg) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;
  uint32_t bit;
  uint32_t mask;

  /* No "interrupts" in the device tree means nothing delivers this. */
  if (!self->has_irq) {
    return ENOTSUP;
  }

  zynq_gpio_locate(self, pin, &bank, &bit);
  mask = 1u << bit;

  if ((self->has_trigger[bank] & mask) == 0) {
    return EINVAL;
  }

  self->pins[pin].handler = handler;
  self->pins[pin].arg = arg;

  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_STAT(bank)) = mask;
  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_EN(bank)) = mask;

  return 0;
}

static int zynq_gpio_pin_irq_disable(rtems_gpio_drv_ctrl* ctrl, uint32_t pin) {
  zynq_gpio_ctrl* self = zynq_gpio_downcast(ctrl);
  uint32_t bank;
  uint32_t bit;

  zynq_gpio_locate(self, pin, &bank, &bit);

  /* The hardware first, so the handler is not called after it is gone. */
  ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_DIS(bank)) = 1u << bit;
  self->pins[pin].handler = NULL;

  return 0;
}

static const rtems_gpio_drv_handlers zynq_gpio_handlers = {
    .pin_get_info = zynq_gpio_pin_get_info,
    .pin_configure = zynq_gpio_pin_configure,
    .pin_get_config = zynq_gpio_pin_get_config,
    .pin_release = zynq_gpio_pin_release,
    .pin_get = zynq_gpio_pin_get,
    .pin_set = zynq_gpio_pin_set,
    .pin_toggle = zynq_gpio_pin_toggle,
    .pin_get_multiple = zynq_gpio_pin_get_multiple,
    .pin_set_multiple = zynq_gpio_pin_set_multiple,
    .pin_irq_enable = zynq_gpio_pin_irq_enable,
    .pin_irq_disable = zynq_gpio_pin_irq_disable};

static bool zynq_gpio_node_enabled(const void* fdt, int node) {
  int len;
  const char* status = fdt_getprop(fdt, node, "status", &len);

  if (status == NULL || len <= 0) {
    return true;
  }

  return strcmp(status, "okay") == 0 || strcmp(status, "ok") == 0;
}

static const zynq_gpio_variant* zynq_gpio_match(const void* fdt, int node) {
  size_t i;

  for (i = 0; i < RTEMS_ARRAY_SIZE(zynq_gpio_variants); ++i) {
    const zynq_gpio_variant* variant = &zynq_gpio_variants[i];

    if (fdt_node_check_compatible(fdt, node, variant->compatible) == 0) {
      return variant;
    }
  }

  return NULL;
}

static int zynq_gpio_find(const void* fdt) {
  size_t i;

  for (i = 0; i < RTEMS_ARRAY_SIZE(zynq_gpio_variants); ++i) {
    int node = -1;

    for (;;) {
      node = fdt_node_offset_by_compatible(fdt, node,
                                           zynq_gpio_variants[i].compatible);
      if (node < 0) {
        break;
      }

      if (zynq_gpio_node_enabled(fdt, node)) {
        return node;
      }
    }
  }

  return -1;
}

/*
 * The parent's #address-cells is what makes one driver read both trees: a
 * Zynq node carries a one cell address and a ZynqMP node a two cell one,
 * and neither says so itself.
 */
static int zynq_gpio_reg_base(const void* fdt, int node, uintptr_t* base) {
  int parent = fdt_parent_offset(fdt, node);
  int cells;
  int len;
  int i;
  const fdt32_t* reg;
  uint64_t address = 0;

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

  for (i = 0; i < cells; ++i) {
    address = (address << 32) | fdt32_ld(&reg[i]);
  }

  *base = (uintptr_t)address;

  if (address == 0 || (uint64_t)*base != address) {
    return EINVAL;
  }

  return 0;
}

static bool zynq_gpio_irq_vector(const void* fdt, int node, uint32_t* vector) {
  int len;
  const fdt32_t* interrupts = fdt_getprop(fdt, node, "interrupts", &len);

  if (interrupts == NULL || len < 3 * (int)sizeof(*interrupts)) {
    return false;
  }

  /* A GIC specifier: 0 is an SPI, which starts at 32, and 1 a PPI, at 16. */
  *vector =
      (fdt32_ld(&interrupts[0]) == 0 ? 32u : 16u) + fdt32_ld(&interrupts[1]);

  return true;
}

/*
 * The child nodes annotate pins that already exist, so one naming a pin the
 * controller does not have is a mistake in the tree rather than something
 * to skip quietly: a board that reserved pin 200 believes pin 200 is safe.
 */
static int zynq_gpio_read_pin_nodes(zynq_gpio_ctrl* self, const void* fdt,
                                    int node) {
  int child;

  for (child = fdt_first_subnode(fdt, node); child >= 0;
       child = fdt_next_subnode(fdt, child)) {
    const fdt32_t* number;
    uint32_t bank;
    uint32_t bit;
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

    zynq_gpio_locate(self, pin, &bank, &bit);
    mask = 1u << bit;

    self->pins[pin].label = fdt_getprop(fdt, child, "rtems,label", NULL);
    self->pins[pin].owner = fdt_getprop(fdt, child, "rtems,owner", NULL);

    if (fdt_getprop(fdt, child, "rtems,reserved", NULL) != NULL) {
      self->reserved[bank] |= mask;
    }

    if (fdt_getprop(fdt, child, "rtems,strapping", NULL) != NULL) {
      self->strapping[bank] |= mask;
    }

    if (fdt_getprop(fdt, child, "rtems,no-pad", NULL) != NULL) {
      self->no_pad[bank] |= mask;
    }
  }

  return 0;
}

int zynq_gpio_register(const void* fdt, int node, const char* path) {
  zynq_gpio_ctrl* self = &zynq_gpio_instance;
  const zynq_gpio_variant* variant;
  const fdt32_t* ngpios;
  uintptr_t regs;
  uint32_t limit;
  uint32_t total;
  uint32_t pins;
  uint32_t bank;
  uint32_t vector = 0;
  int len;
  int err;

  if (fdt == NULL || path == NULL) {
    return EINVAL;
  }

  if (zynq_gpio_registered) {
    return EBUSY;
  }

  if (fdt_check_header(fdt) != 0) {
    return EINVAL;
  }

  if (node < 0) {
    node = zynq_gpio_find(fdt);
    if (node < 0) {
      return EINVAL;
    }
  }

  variant = zynq_gpio_match(fdt, node);
  if (variant == NULL) {
    return EINVAL;
  }

  err = zynq_gpio_reg_base(fdt, node, &regs);
  if (err != 0) {
    return err;
  }

  total = 0;
  for (bank = 0; bank < variant->bank_count; ++bank) {
    total += variant->bank_width[bank];
  }

  /* The static storage below is sized by ZYNQ_GPIO_PIN_COUNT_MAX, so a part
   * added to the table without raising it is caught here and not by a write
   * past the end of an array. */
  if (total > ZYNQ_GPIO_PIN_COUNT_MAX) {
    return EINVAL;
  }

  limit = total;
  ngpios = fdt_getprop(fdt, node, "ngpios", &len);
  if (ngpios != NULL && len >= (int)sizeof(*ngpios)) {
    limit = fdt32_ld(ngpios);

    if (limit == 0 || limit > total) {
      return EINVAL;
    }
  }

  memset(self, 0, sizeof(*self));
  self->regs = regs;
  self->mio_banks = variant->mio_banks;

  pins = 0;
  for (bank = 0; bank < variant->bank_count && pins < limit; ++bank) {
    uint32_t width = variant->bank_width[bank];

    if (pins + width > limit) {
      width = limit - pins;
    }

    self->bank_first[bank] = pins;
    self->bank_mask[bank] = zynq_gpio_width_mask(width);
    self->bank_count = bank + 1;
    pins += width;
  }

  self->base.handlers = &zynq_gpio_handlers;
  self->base.pin_count = pins;
  self->base.name = variant->name;

  /* Memory mapped registers, so nothing here waits and a caller may use
   * this controller from interrupt context. */
  self->base.can_block = false;
  self->base.active_low = self->active_low;
  self->base.scratch = self->scratch;

  err = zynq_gpio_read_pin_nodes(self, fdt, node);
  if (err != 0) {
    return err;
  }

  err = rtems_gpio_drv_ctrl_init(&self->base);
  if (err != 0) {
    return err;
  }

  /* Nothing is configured yet, so anything pending is from before us. */
  for (bank = 0; bank < self->bank_count; ++bank) {
    ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_DIS(bank)) = self->bank_mask[bank];
    ZYNQ_GPIO_REG(self, ZYNQ_GPIO_INT_STAT(bank)) = self->bank_mask[bank];
  }

  if (zynq_gpio_irq_vector(fdt, node, &vector)) {
    rtems_status_code sc;

    sc = rtems_interrupt_handler_install(
        vector, variant->name, RTEMS_INTERRUPT_SHARED, zynq_gpio_isr, self);
    if (sc != RTEMS_SUCCESSFUL) {
      return EIO;
    }

    self->has_irq = true;
  }

  if (rtems_gpio_drv_ctrl_register(&self->base, path) != 0) {
    err = errno;

    if (self->has_irq) {
      (void)rtems_interrupt_handler_remove(vector, zynq_gpio_isr, self);
      self->has_irq = false;
    }

    return err;
  }

  zynq_gpio_registered = true;

  return 0;
}
