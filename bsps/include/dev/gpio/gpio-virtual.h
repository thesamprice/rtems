/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSDeviceGPIOVirtual
 *
 * @brief This header file provides the interfaces of the virtual GPIO
 *   device driver.
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

#ifndef LIBBSP_SHARED_DEV_GPIO_GPIO_VIRTUAL_H
#define LIBBSP_SHARED_DEV_GPIO_GPIO_VIRTUAL_H

#include <dev/gpio/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup RTEMSDeviceGPIOVirtual Virtual GPIO
 *
 * @ingroup RTEMSDeviceDrivers
 *
 * @brief This group contains a GPIO controller whose pins are callbacks
 *   the registrant supplies rather than pads.
 *
 * A board exposes things that behave like a GPIO and are not pads: a pin
 * on an expander behind a bus, a clock or power gate, an enable for a
 * subsystem such as a radio or a PHY, or a signal one task drives and
 * another waits on.  Each becomes a pin here, and the consumer drives it
 * with rtems_gpio_pin_set() and rtems_gpio_pin_get() without knowing what
 * is underneath.
 *
 * The registrant supplies a table of rtems_gpio_virtual_pin, one entry per
 * logical pin, and the storage the controller needs.  Nothing is
 * allocated.
 *
 * @par Capabilities
 *
 * Each pin declares its own capabilities, so a proxy pin that interrupts
 * and a clock gate that only drives can sit in one table.  What a pin may
 * declare is limited to #RTEMS_GPIO_VIRTUAL_CAPS, and each capability must
 * be backed by the callback that implements it:
 * #RTEMS_GPIO_CAP_OUTPUT needs rtems_gpio_virtual_pin::set and
 * #RTEMS_GPIO_CAP_INPUT needs rtems_gpio_virtual_pin::get.
 * rtems_gpio_virtual_register() refuses a table that breaks this rather
 * than dropping the capability, because a dropped capability turns into a
 * pin that is silently not what the board says it is.
 *
 * A pin with no set callback therefore reports no #RTEMS_GPIO_CAP_OUTPUT,
 * and configuring it as an output is refused with ENOTSUP by the generic
 * layer along with every other capability it does not have.
 *
 * @par Reentrancy
 *
 * The generic layer holds the controller mutex across every callback.  So
 * a callback must not call any rtems_gpio_*() function on the controller
 * it belongs to; that deadlocks.  A proxy pin forwards to a different
 * controller, which is a different mutex and is safe.
 *
 * @par Interrupts
 *
 * Nothing here polls and writing a pin does not raise its interrupt.  The
 * registrant calls rtems_gpio_virtual_raise() when the thing behind a pin
 * changes, which is what makes "the subsystem is up now" an interrupt the
 * consumer can wait on.  Whether the level agrees with the pin's trigger
 * is not checked: what counts as an edge is the registrant's to decide.
 *
 * @{
 */

/**
 * @brief This constant contains the capabilities a pin of this controller
 *   may declare.
 *
 * Bias, drive, drive strength and debounce are electrical properties of a
 * pad, and this controller has no pad and no way to pass them to a
 * callback.  A pin that declared one would have the generic layer accept a
 * configuration nothing below it applies.
 */
#define RTEMS_GPIO_VIRTUAL_CAPS                                                \
  (RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_OUTPUT |                              \
   RTEMS_GPIO_CAP_OUTPUT_READBACK | RTEMS_GPIO_CAP_EDGE_RISING |               \
   RTEMS_GPIO_CAP_EDGE_FALLING | RTEMS_GPIO_CAP_EDGE_BOTH |                    \
   RTEMS_GPIO_CAP_LEVEL_HIGH | RTEMS_GPIO_CAP_LEVEL_LOW)

/**
 * @brief This type represents the callback that reads a pin.
 *
 * @param pin is the logical pin of this controller being read.
 *
 * @param arg is rtems_gpio_virtual_pin::arg.
 *
 * @param[out] value is the pointer to an int object.  When the call is
 *   successful, the physical level of the pin, 0 or 1, will be stored in
 *   this object.  Physical: #RTEMS_GPIO_FLAG_ACTIVE_LOW is applied above
 *   this callback and must not be applied again in it.
 *
 * @retval 0 Successful operation.
 *
 * @return Returns an errno otherwise, which the caller of
 *   rtems_gpio_pin_get() sees.
 */
typedef int (*rtems_gpio_virtual_get)(uint32_t pin, void* arg, int* value);

/**
 * @brief This type represents the callback that writes a pin.
 *
 * @param pin is the logical pin of this controller being written.
 *
 * @param arg is rtems_gpio_virtual_pin::arg.
 *
 * @param value is the physical level to apply, 0 or 1.  Physical, with the
 *   same meaning as in rtems_gpio_virtual_get.
 *
 * @retval 0 Successful operation.
 *
 * @return Returns an errno otherwise, which the caller of
 *   rtems_gpio_pin_set() sees.
 */
typedef int (*rtems_gpio_virtual_set)(uint32_t pin, void* arg, int value);

/**
 * @brief This structure provides one pin of a virtual GPIO controller.
 *
 * One entry per logical pin, in pin order.  An entry that is all zero is a
 * pin that exists, can do nothing, and is refused for every configuration
 * that asks for anything.
 */
typedef struct {
  /**
   * @brief This member reads the pin, or is NULL where it cannot be read.
   */
  rtems_gpio_virtual_get get;

  /**
   * @brief This member writes the pin, or is NULL where it cannot be
   *   written.
   */
  rtems_gpio_virtual_set set;

  /**
   * @brief This member is passed to get and set.
   */
  void* arg;

  /**
   * @brief This member contains what this pin can do, from
   *   #RTEMS_GPIO_VIRTUAL_CAPS.
   */
  uint32_t capabilities;

  /**
   * @brief This member contains #RTEMS_GPIO_PIN_AVAILABLE, or some of
   *   #RTEMS_GPIO_PIN_RESERVED, #RTEMS_GPIO_PIN_STRAPPING and
   *   #RTEMS_GPIO_PIN_NO_PAD, and is reported unchanged.
   */
  uint32_t flags;

  /**
   * @brief This member contains what the board calls this pin, or NULL.
   *
   * Not copied, so it must outlive the controller.
   */
  const char* name;

  /**
   * @brief This member contains what holds this pin, or NULL.
   *
   * Meaningful with #RTEMS_GPIO_PIN_RESERVED, and not copied.
   */
  const char* owner;
} rtems_gpio_virtual_pin;

/**
 * @brief This structure provides the driver's state for one pin.
 *
 * The registrant allocates one per pin and passes the array to
 * rtems_gpio_virtual_register(), which is where the driver's need for
 * writable storage is met without allocating any.  Nothing outside the
 * driver reads or writes it.
 */
typedef struct {
  /**
   * @brief This member contains the configuration applied to the pin.
   */
  rtems_gpio_config config;

  /**
   * @brief This member contains the handler the pin's interrupt goes to,
   *   or NULL.
   */
  rtems_gpio_irq_handler handler;

  /**
   * @brief This member contains the argument passed to handler.
   */
  void* irq_arg;

  /**
   * @brief This member contains the last physical level written to an
   *   output, which is what a pin with no get callback reads back.
   */
  int level;

  /**
   * @brief This member is true, if the pin is configured, otherwise false.
   */
  bool in_use;
} rtems_gpio_virtual_pin_state;

/**
 * @brief This structure provides a virtual GPIO controller.
 *
 * Opaque to the registrant apart from its lifetime: it must outlive the
 * device node.  A BSP normally makes it a static object.
 */
typedef struct {
  /**
   * @brief This member contains the generic controller.
   */
  rtems_gpio_drv_ctrl base;

  /**
   * @brief This member contains the pin table.
   */
  const rtems_gpio_virtual_pin* pins;

  /**
   * @brief This member contains the per-pin state.
   */
  rtems_gpio_virtual_pin_state* state;

  /**
   * @brief This member serialises rtems_gpio_virtual_raise() against the
   *   enabling and disabling of an interrupt.
   */
  RTEMS_INTERRUPT_LOCK_MEMBER(lock)
} rtems_gpio_virtual_ctrl;

/**
 * @brief This structure provides what a virtual GPIO controller is made
 *   of, as one argument to rtems_gpio_virtual_register().
 */
typedef struct {
  /**
   * @brief This member contains what the controller calls itself, or NULL.
   *
   * Not copied, so it must outlive the controller.
   */
  const char* name;

  /**
   * @brief This member contains the pin table, pin_count entries long.
   */
  const rtems_gpio_virtual_pin* pins;

  /**
   * @brief This member contains the per-pin state, pin_count entries long.
   */
  rtems_gpio_virtual_pin_state* state;

  /**
   * @brief This member contains storage for the generic layer's active low
   *   bitmap, RTEMS_GPIO_BITMAP_WORDS() of pin_count words.
   */
  uint32_t* active_low;

  /**
   * @brief This member contains storage for the generic layer's scratch
   *   bitmap, RTEMS_GPIO_BITMAP_WORDS() of pin_count words.
   */
  uint32_t* scratch;

  /**
   * @brief This member contains the number of pins the controller
   *   publishes.
   */
  uint32_t pin_count;

  /**
   * @brief This member is true, if a callback of this controller may
   *   block, otherwise false.
   *
   * The registrant's to answer, because only it knows what the callbacks
   * do: a pin on an I2C expander blocks and a software flag does not.  A
   * controller that says false may be driven from interrupt context, so
   * saying it of a callback that waits is how a caller ends up blocking in
   * an ISR.
   */
  bool can_block;
} rtems_gpio_virtual_config;

/**
 * @brief Defines the storage a virtual GPIO controller of @a pin_count
 *   pins needs, as static objects named after @a designator.
 *
 * The three arrays have to agree on the pin count, and nothing at run time
 * can tell that they do not, so they are best defined together.  Do not
 * add a ";" after this macro.
 *
 * @param designator is the identifier the objects are named after.
 *
 * @param pin_count is the number of pins the controller publishes.
 */
#define RTEMS_GPIO_VIRTUAL_STORAGE_DEFINE(designator, pin_count)               \
  static rtems_gpio_virtual_pin_state designator##_state[pin_count];           \
  static uint32_t designator##_active_low[RTEMS_GPIO_BITMAP_WORDS(pin_count)]; \
  static uint32_t designator##_scratch[RTEMS_GPIO_BITMAP_WORDS(pin_count)];

/**
 * @brief Initializes the storage members of an rtems_gpio_virtual_config
 *   from the objects RTEMS_GPIO_VIRTUAL_STORAGE_DEFINE() made.
 *
 * @param designator is the identifier passed to
 *   RTEMS_GPIO_VIRTUAL_STORAGE_DEFINE().
 *
 * @param count is the pin count passed to it.
 */
#define RTEMS_GPIO_VIRTUAL_STORAGE(designator, count)                          \
  .pin_count = (count), .state = designator##_state,                           \
  .active_low = designator##_active_low, .scratch = designator##_scratch

/**
 * @brief Registers a virtual GPIO controller as a device node.
 *
 * @a config is read here and not kept.  What it points at is kept and not
 * copied, so the pin table, the storage and the names must outlive the
 * node.  The per-pin state is cleared here.
 *
 * @param[out] ctrl is the controller to build, which must outlive the
 *   node.
 *
 * @param config is what the controller is made of.
 *
 * @param path is where the node goes, for example "/dev/gpio-virtual".
 *
 * @retval 0 Successful operation.
 * @retval EINVAL An argument is NULL, the controller publishes no pins,
 *   the storage is missing, or a pin declares a capability outside
 *   #RTEMS_GPIO_VIRTUAL_CAPS or one its callbacks cannot back.
 *
 * @return Returns an errno otherwise, from the registration of the node.
 */
int rtems_gpio_virtual_register(rtems_gpio_virtual_ctrl* ctrl,
                                const rtems_gpio_virtual_config* config,
                                const char* path);

/**
 * @brief Raises a pin's interrupt.
 *
 * The handler runs synchronously in the calling context and with no lock
 * of this controller held, so the registrant decides the context and must
 * make it agree with rtems_gpio_virtual_config::can_block.
 *
 * Calling this from a get or set callback delivers the handler underneath
 * the controller mutex that callback already holds, and the handler must
 * then touch nothing of this controller.
 *
 * @param ctrl is a controller rtems_gpio_virtual_register() returned 0
 *   for.
 *
 * @param pin is the logical pin whose interrupt to raise.
 *
 * @retval 0 The handler was called.
 * @retval EINVAL @a ctrl is NULL.
 * @retval ENODEV @a pin is not a pin of this controller.
 * @retval ENOENT No handler is enabled on the pin, so nothing was called.
 */
int rtems_gpio_virtual_raise(rtems_gpio_virtual_ctrl* ctrl, uint32_t pin);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LIBBSP_SHARED_DEV_GPIO_GPIO_VIRTUAL_H */
