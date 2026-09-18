/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSGenericGPIOAPI
 *
 * @brief This header file provides the interfaces of the generic GPIO
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

#ifndef _DEV_GPIO_GPIO_H
#define _DEV_GPIO_GPIO_H

#include <rtems.h>
#include <rtems/thread.h>

#include <stdint.h>
#include <sys/ioccom.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup RTEMSGenericGPIOAPI Generic GPIO API
 *
 * @ingroup RTEMSDeviceDrivers
 *
 * @brief A GPIO controller as an IMFS device node driven by ioctl().
 *
 * A controller is a driver in the BSP.  It owns the pins: which pad a pin
 * is, which bank it lives in, how it is muxed, and whether the board permits
 * it to be used at all are the BSP's to decide, and nothing above the BSP
 * can reach a register.  This API configures the pins the BSP has published
 * and does nothing else.
 *
 * Pins are numbered logically, from 0 to rtems_gpio_info::pin_count - 1,
 * with no holes.  Hardware is rarely like that -- pins come in banks, banks
 * have gaps, and a part may bring only some of a bank out to pads -- so the
 * mapping from a logical pin to whatever the silicon calls it belongs to the
 * driver, which is the only place that knows it.
 *
 * A driver need not implement everything.  A handler left NULL is reported
 * as ENOTSUP, so a controller that can only read and write pins is a
 * complete and legal controller, and a caller that wants an open-drain
 * output on a part that has none is told so rather than silently given a
 * push-pull one.
 *
 * Every operation is an ioctl() on the node, and the rtems_gpio_*()
 * functions below are wrappers over those ioctl() calls.  Use the wrappers;
 * the command numbers are published so that a caller which already holds a
 * descriptor for another reason is not forced to.
 *
 * @{
 */

/**
 * @brief The maximum length of a pin name, including the terminating NUL.
 */
#define RTEMS_GPIO_NAME_MAX 32

/**
 * @brief The maximum number of pins one rtems_gpio_pin_list may carry.
 *
 * Sized so that the structure stays small enough to be a sensible automatic
 * variable, since it is what an ioctl() argument is in practice.
 */
#define RTEMS_GPIO_PIN_LIST_MAX 32

/**
 * @brief The direction of a pin.
 */
typedef enum {
  /**
   * @brief The pin is not configured.
   *
   * What a pin reads as before rtems_gpio_pin_configure() is called on it,
   * and what it returns to after rtems_gpio_pin_release().
   */
  RTEMS_GPIO_DIRECTION_NONE = 0,

  /**
   * @brief The pin is an input.
   */
  RTEMS_GPIO_DIRECTION_INPUT,

  /**
   * @brief The pin is an output.
   */
  RTEMS_GPIO_DIRECTION_OUTPUT,

  /**
   * @brief The pin is an output that can also be read back.
   *
   * Not the same as an output.  Reading an output tells you what was last
   * written on many parts and what the pad is actually at on others, and a
   * caller that needs the second has to ask for it.
   */
  RTEMS_GPIO_DIRECTION_BIDIRECTIONAL
} rtems_gpio_direction;

/**
 * @brief The bias applied to a pin.
 */
typedef enum {
  RTEMS_GPIO_BIAS_NONE = 0,
  RTEMS_GPIO_BIAS_PULL_UP,
  RTEMS_GPIO_BIAS_PULL_DOWN
} rtems_gpio_bias;

/**
 * @brief How an output pin drives.
 */
typedef enum {
  RTEMS_GPIO_DRIVE_PUSH_PULL = 0,
  RTEMS_GPIO_DRIVE_OPEN_DRAIN,
  RTEMS_GPIO_DRIVE_OPEN_SOURCE
} rtems_gpio_drive;

/**
 * @brief What makes a pin raise an interrupt.
 */
typedef enum {
  RTEMS_GPIO_TRIGGER_NONE = 0,
  RTEMS_GPIO_TRIGGER_EDGE_RISING,
  RTEMS_GPIO_TRIGGER_EDGE_FALLING,
  RTEMS_GPIO_TRIGGER_EDGE_BOTH,
  RTEMS_GPIO_TRIGGER_LEVEL_HIGH,
  RTEMS_GPIO_TRIGGER_LEVEL_LOW
} rtems_gpio_trigger;

/**
 * @brief Whether a pin is a pad on the part or something the driver makes up.
 */
typedef enum {
  /**
   * @brief A pad on the part the controller is part of.
   */
  RTEMS_GPIO_PIN_PHYSICAL = 0,

  /**
   * @brief A pin the driver implements rather than one the part has.
   *
   * An expander behind I2C or SPI, a bit in a shift register, a line on an
   * FPGA fabric, or a pin that exists only in software so that a test can
   * drive a consumer with no hardware present.  A virtual pin is otherwise
   * an ordinary pin: it configures, reads, writes and reports through the
   * same calls.
   *
   * The distinction is published because it is the one a caller cannot infer
   * and does have to account for.  A virtual pin's access may block, may
   * take milliseconds, and is usually not safe from interrupt context, none
   * of which is true of a register write.
   */
  RTEMS_GPIO_PIN_VIRTUAL
} rtems_gpio_pin_kind;

/**
 * @name Pin capabilities
 *
 * What a pin is able to do, as reported by rtems_gpio_pin_get_info().  A
 * caller can ask before it configures anything, which is the difference
 * between choosing a pin that works and finding out from a failed
 * configuration after the pad has already been muxed.
 *
 * @{
 */

#define RTEMS_GPIO_CAP_INPUT          ( 1u << 0 )
#define RTEMS_GPIO_CAP_OUTPUT         ( 1u << 1 )
#define RTEMS_GPIO_CAP_BIDIRECTIONAL  ( 1u << 2 )
#define RTEMS_GPIO_CAP_PULL_UP        ( 1u << 3 )
#define RTEMS_GPIO_CAP_PULL_DOWN      ( 1u << 4 )
#define RTEMS_GPIO_CAP_OPEN_DRAIN     ( 1u << 5 )
#define RTEMS_GPIO_CAP_OPEN_SOURCE    ( 1u << 6 )
#define RTEMS_GPIO_CAP_DRIVE_STRENGTH ( 1u << 7 )
#define RTEMS_GPIO_CAP_DEBOUNCE       ( 1u << 8 )
#define RTEMS_GPIO_CAP_INVERT         ( 1u << 9 )
#define RTEMS_GPIO_CAP_EDGE_RISING    ( 1u << 10 )
#define RTEMS_GPIO_CAP_EDGE_FALLING   ( 1u << 11 )
#define RTEMS_GPIO_CAP_EDGE_BOTH      ( 1u << 12 )
#define RTEMS_GPIO_CAP_LEVEL_HIGH     ( 1u << 13 )
#define RTEMS_GPIO_CAP_LEVEL_LOW      ( 1u << 14 )
#define RTEMS_GPIO_CAP_WAKEUP         ( 1u << 15 )

/** @} */

/**
 * @name Pin availability
 *
 * Whether the board permits a pin to be used, which is a different question
 * from whether the silicon can.  Both are the BSP's to answer and neither
 * can be worked out from above it.
 *
 * @{
 */

/**
 * @brief The pin exists and is free to use.
 */
#define RTEMS_GPIO_PIN_AVAILABLE  0u

/**
 * @brief The pin must never be handed out.
 *
 * It belongs to something the board cannot do without: a flash pad, a power
 * rail select, the console UART, a USB differential pair.  Configuring it
 * does not fail in an obvious way, it breaks the board, so
 * rtems_gpio_pin_configure() refuses it.
 */
#define RTEMS_GPIO_PIN_RESERVED   ( 1u << 0 )

/**
 * @brief The pin is in use by another driver or by an earlier caller.
 */
#define RTEMS_GPIO_PIN_IN_USE     ( 1u << 1 )

/**
 * @brief Usable, but the level held at reset changes how the board starts.
 *
 * A strapping or boot-mode pin.  Not refused -- using one is a legitimate
 * thing to do and on a small part it is sometimes unavoidable -- but a
 * caller that has a choice should be able to make it knowingly.
 */
#define RTEMS_GPIO_PIN_STRAPPING  ( 1u << 2 )

/**
 * @brief The pin is not brought out to a pad in this package.
 *
 * The controller has it, this board cannot reach it.
 */
#define RTEMS_GPIO_PIN_NO_PAD     ( 1u << 3 )

/** @} */

/**
 * @name Configuration flags
 *
 * @{
 */

/**
 * @brief The pin is active low.
 *
 * rtems_gpio_pin_get() and rtems_gpio_pin_set() then speak in logical
 * levels, and the driver inverts.  Without this a caller inverts for itself
 * and every consumer of the pin has to agree about it.
 */
#define RTEMS_GPIO_FLAG_ACTIVE_LOW ( 1u << 0 )

/**
 * @brief The pin may wake the system from a low power state.
 */
#define RTEMS_GPIO_FLAG_WAKEUP     ( 1u << 1 )

/** @} */

/**
 * @brief How a pin is configured.
 *
 * The same structure sets a configuration and reports one, so that what
 * rtems_gpio_pin_get_configuration() returns can be compared against what
 * was asked for.  They differ more often than one might expect: a driver is
 * allowed to round a drive strength or a debounce interval to what the
 * hardware has, and reporting the rounded value is how a caller finds out.
 */
typedef struct {
  /**
   * @brief Input, output, or both.
   */
  rtems_gpio_direction direction;

  /**
   * @brief The pull resistor, if any.
   */
  rtems_gpio_bias bias;

  /**
   * @brief How an output drives.  Ignored for an input.
   */
  rtems_gpio_drive drive;

  /**
   * @brief What raises an interrupt on this pin, if anything.
   */
  rtems_gpio_trigger trigger;

  /**
   * @brief RTEMS_GPIO_FLAG_* applied to the pin.
   */
  uint32_t flags;

  /**
   * @brief Drive strength in microamperes, or 0 for the driver's default.
   *
   * Microamperes rather than an index, because an index means nothing
   * without the part's table in front of you and cannot be compared across
   * two parts.  A driver rounds to what it has and reports the rounded
   * value back.
   */
  uint32_t drive_strength;

  /**
   * @brief Debounce interval in microseconds, or 0 for none.
   *
   * Microseconds, and not clock ticks: a value in ticks means something
   * different in an application configured for a different tick rate, which
   * makes it useless in a driver and a trap in a configuration file.
   */
  uint32_t debounce;

  /**
   * @brief The level an output takes when it is configured.
   *
   * Logical, so RTEMS_GPIO_FLAG_ACTIVE_LOW applies to it.  Ignored unless
   * the direction is an output.  Setting the level as part of configuring
   * the pin is what keeps an output from glitching to the wrong level
   * between being made an output and being written.
   */
  int initial_value;
} rtems_gpio_config;

/**
 * @brief What a pin is, as opposed to how it is configured.
 *
 * Static for the life of the controller, apart from
 * rtems_gpio_pin_info::flags, which tracks whether the pin is in use.
 */
typedef struct {
  /**
   * @brief The logical pin this describes.
   *
   * Filled in by the caller before RTEMS_GPIO_IOCTL_PIN_GET_INFO and
   * returned unchanged, so that the answer carries its own question.
   */
  uint32_t pin;

  /**
   * @brief Whether the pin is a pad on the part or one the driver provides.
   */
  rtems_gpio_pin_kind kind;

  /**
   * @brief RTEMS_GPIO_CAP_* the pin supports.
   */
  uint32_t capabilities;

  /**
   * @brief RTEMS_GPIO_PIN_* availability of the pin.
   */
  uint32_t flags;

  /**
   * @brief What the board calls this pin, or an empty string.
   *
   * A silkscreen label, a net name, or the function a board fixed to the
   * pin: "LED0", "SDA", "BOOT".  A caller must not require it -- most
   * controllers will leave it empty -- but where a board has names, this is
   * where they belong, and it is what lets a configuration name a pin
   * rather than count pads.
   */
  char name[ RTEMS_GPIO_NAME_MAX ];
} rtems_gpio_pin_info;

/**
 * @brief What a controller is.
 */
typedef struct {
  /**
   * @brief The number of logical pins, which are 0 to pin_count - 1.
   */
  uint32_t pin_count;

  /**
   * @brief What the controller calls itself, for diagnostics.
   */
  char name[ RTEMS_GPIO_NAME_MAX ];
} rtems_gpio_info;

/**
 * @brief Several pins and their values, for the operations that act on more
 *   than one pin at a time.
 *
 * The point of these is that the pins change together.  A driver whose
 * hardware has a set or a clear register does them in one write; one whose
 * hardware does not still does them without releasing the lock in between,
 * so no other caller sees the intermediate state.  Writing the pins one at a
 * time through rtems_gpio_pin_set() is not the same operation and a caller
 * that needs a bus to change atomically cannot substitute it.
 */
typedef struct {
  /**
   * @brief How many entries of pins and values are in use.
   */
  uint32_t count;

  /**
   * @brief The logical pins to act on.
   */
  uint32_t pins[ RTEMS_GPIO_PIN_LIST_MAX ];

  /**
   * @brief One bit per entry of pins, in the same order.
   *
   * Bit 0 is pins[0].  Logical levels, so RTEMS_GPIO_FLAG_ACTIVE_LOW
   * applies per pin.
   */
  uint32_t values;
} rtems_gpio_pin_list;

/**
 * @brief The interrupt handler a pin calls.
 *
 * @param pin The logical pin that raised the interrupt.
 * @param arg The argument given to rtems_gpio_pin_irq_enable().
 *
 * Runs in whatever context the driver raises it from.  For a physical pin
 * that is usually interrupt context, so the handler is bound by the same
 * rules as any other RTEMS interrupt handler.  For a virtual pin behind a
 * bus it is usually a task, since reading an expander over I2C cannot be
 * done from an ISR.  rtems_gpio_pin_get_info() is how a caller tells which
 * it has.
 */
typedef void ( *rtems_gpio_irq_handler )( uint32_t pin, void *arg );

typedef struct rtems_gpio_ctrl rtems_gpio_ctrl;

/**
 * @brief What a GPIO driver implements.
 *
 * Any handler may be NULL, and the generic layer then answers ENOTSUP for
 * the operations that need it.  A driver is not obliged to grow stubs for
 * hardware it does not have.
 *
 * Each returns 0 on success or a positive errno on failure.  Returning
 * ENOTSUP for a request the hardware cannot meet -- an open-drain output on
 * a part without one, a level-triggered interrupt on a part with only edges
 * -- is correct and expected; quietly substituting something else is not.
 */
typedef struct {
  /**
   * @brief Reports what a pin is.
   *
   * Required: a driver that does not implement this cannot be registered,
   * because a caller has then no way to find out anything about a pin
   * before configuring it, which is the situation this API exists to end.
   */
  int ( *pin_get_info )(
    rtems_gpio_ctrl     *ctrl,
    uint32_t             pin,
    rtems_gpio_pin_info *info
  );

  /**
   * @brief Applies a configuration to a pin.
   *
   * The generic layer has already checked that the pin is in range and is
   * neither reserved nor in use, and that every capability the
   * configuration asks for is one the pin reported.  What is left for the
   * driver is the hardware.
   *
   * A driver that rounds a value it cannot meet exactly should write what it
   * actually applied back into @a config, which is what the caller then sees
   * from rtems_gpio_pin_get_configuration().
   */
  int ( *pin_configure )(
    rtems_gpio_ctrl   *ctrl,
    uint32_t           pin,
    rtems_gpio_config *config
  );

  /**
   * @brief Reports how a pin is configured now.
   *
   * Only called for a pin the driver reports as in use, so a driver need
   * not handle the unconfigured case.  What it reports is what it applied,
   * including any value it rounded, which is how a caller finds out that
   * the 8 mA it asked for became the 10 mA the part has.
   */
  int ( *pin_get_config )(
    rtems_gpio_ctrl   *ctrl,
    uint32_t           pin,
    rtems_gpio_config *config
  );

  /**
   * @brief Returns a pin to its unconfigured state.
   */
  int ( *pin_release )( rtems_gpio_ctrl *ctrl, uint32_t pin );

  /**
   * @brief Reads a pin.
   *
   * @param[out] value 0 or 1, logical.
   */
  int ( *pin_get )( rtems_gpio_ctrl *ctrl, uint32_t pin, int *value );

  /**
   * @brief Writes a pin.
   *
   * @param value 0 or 1, logical.
   */
  int ( *pin_set )( rtems_gpio_ctrl *ctrl, uint32_t pin, int value );

  /**
   * @brief Inverts a pin's current level.
   *
   * Separate from a read and a write because on most parts it is one
   * register access, and because a read-modify-write from the caller is not
   * atomic against another caller.
   */
  int ( *pin_toggle )( rtems_gpio_ctrl *ctrl, uint32_t pin );

  /**
   * @brief Reads several pins as one operation.
   */
  int ( *pin_get_multiple )( rtems_gpio_ctrl *ctrl, rtems_gpio_pin_list *list );

  /**
   * @brief Writes several pins as one operation.
   */
  int ( *pin_set_multiple )(
    rtems_gpio_ctrl           *ctrl,
    const rtems_gpio_pin_list *list
  );

  /**
   * @brief Starts delivering a pin's interrupt to a handler.
   */
  int ( *pin_irq_enable )(
    rtems_gpio_ctrl       *ctrl,
    uint32_t               pin,
    rtems_gpio_irq_handler handler,
    void                  *arg
  );

  /**
   * @brief Stops delivering a pin's interrupt.
   */
  int ( *pin_irq_disable )( rtems_gpio_ctrl *ctrl, uint32_t pin );

  /**
   * @brief Releases whatever the driver holds.
   *
   * Called when the node is destroyed.  May be NULL for a controller whose
   * storage is static, which most are.
   */
  void ( *destroy )( rtems_gpio_ctrl *ctrl );
} rtems_gpio_handlers;

/**
 * @brief A GPIO controller.
 *
 * A driver embeds this in its own structure and recovers that structure with
 * RTEMS_CONTAINER_OF(), so the generic layer needs no allocation and a
 * controller can be a static object in the BSP.
 */
struct rtems_gpio_ctrl {
  /**
   * @brief What the driver implements.
   */
  const rtems_gpio_handlers *handlers;

  /**
   * @brief The number of logical pins this controller publishes.
   */
  uint32_t pin_count;

  /**
   * @brief What the controller calls itself.
   */
  const char *name;

  /**
   * @brief Serialises access to the controller.
   *
   * Initialised by rtems_gpio_ctrl_init().  Held across every operation, so
   * a driver's handlers do not need locking of their own and two tasks
   * configuring two pins of one controller cannot interleave.
   */
  rtems_mutex mutex;
};

/**
 * @brief A pin and its configuration, as one ioctl() argument.
 */
typedef struct {
  uint32_t          pin;
  rtems_gpio_config config;
} rtems_gpio_pin_config;

/**
 * @brief A pin and its level, as one ioctl() argument.
 */
typedef struct {
  uint32_t pin;
  int      value;
} rtems_gpio_pin_value;

/**
 * @brief A pin and its interrupt handler, as one ioctl() argument.
 */
typedef struct {
  uint32_t               pin;
  rtems_gpio_irq_handler handler;
  void                  *arg;
} rtems_gpio_pin_irq;

/**
 * @name ioctl() commands
 *
 * Every operation is one of these on the controller's node.  The
 * rtems_gpio_*() wrappers below are the supported way to issue them.
 *
 * @{
 */

/**
 * @brief Reports the controller.  The argument is a rtems_gpio_info *.
 */
#define RTEMS_GPIO_IOCTL_GET_INFO _IOR( 'G', 0, rtems_gpio_info )

/**
 * @brief Reports a pin.  The argument is a rtems_gpio_pin_info *, with
 *   rtems_gpio_pin_info::pin set by the caller.
 */
#define RTEMS_GPIO_IOCTL_PIN_GET_INFO _IOWR( 'G', 1, rtems_gpio_pin_info )

/**
 * @brief Configures a pin.  The argument is a rtems_gpio_pin_config *.
 */
#define RTEMS_GPIO_IOCTL_PIN_CONFIGURE _IOWR( 'G', 2, rtems_gpio_pin_config )

/**
 * @brief Reports how a pin is configured.  The argument is a
 *   rtems_gpio_pin_config *, with rtems_gpio_pin_config::pin set by the
 *   caller.
 */
#define RTEMS_GPIO_IOCTL_PIN_GET_CONFIG _IOWR( 'G', 3, rtems_gpio_pin_config )

/**
 * @brief Unconfigures a pin.  The argument is a uint32_t *.
 */
#define RTEMS_GPIO_IOCTL_PIN_RELEASE _IOW( 'G', 4, uint32_t )

/**
 * @brief Reads a pin.  The argument is a rtems_gpio_pin_value *.
 */
#define RTEMS_GPIO_IOCTL_PIN_GET _IOWR( 'G', 5, rtems_gpio_pin_value )

/**
 * @brief Writes a pin.  The argument is a rtems_gpio_pin_value *.
 */
#define RTEMS_GPIO_IOCTL_PIN_SET _IOW( 'G', 6, rtems_gpio_pin_value )

/**
 * @brief Inverts a pin.  The argument is a uint32_t *.
 */
#define RTEMS_GPIO_IOCTL_PIN_TOGGLE _IOW( 'G', 7, uint32_t )

/**
 * @brief Reads several pins.  The argument is a rtems_gpio_pin_list *.
 */
#define RTEMS_GPIO_IOCTL_PIN_GET_MULTIPLE _IOWR( 'G', 8, rtems_gpio_pin_list )

/**
 * @brief Writes several pins.  The argument is a rtems_gpio_pin_list *.
 */
#define RTEMS_GPIO_IOCTL_PIN_SET_MULTIPLE _IOW( 'G', 9, rtems_gpio_pin_list )

/**
 * @brief Enables a pin's interrupt.  The argument is a rtems_gpio_pin_irq *.
 */
#define RTEMS_GPIO_IOCTL_PIN_IRQ_ENABLE _IOW( 'G', 10, rtems_gpio_pin_irq )

/**
 * @brief Disables a pin's interrupt.  The argument is a uint32_t *.
 */
#define RTEMS_GPIO_IOCTL_PIN_IRQ_DISABLE _IOW( 'G', 11, uint32_t )

/** @} */

/**
 * @name Driver side
 *
 * @{
 */

/**
 * @brief Prepares a controller for registration.
 *
 * @param ctrl The controller, whose handlers, pin_count and name the driver
 *   has already set.
 *
 * @retval 0 Successful operation.
 * @retval EINVAL @a ctrl is NULL, has no handlers, has no pin_get_info
 *   handler, or publishes no pins.
 */
int rtems_gpio_ctrl_init( rtems_gpio_ctrl *ctrl );

/**
 * @brief Publishes a controller as a device node.
 *
 * @param ctrl A controller that rtems_gpio_ctrl_init() has returned 0 for.
 * @param path Where the node goes, conventionally "/dev/gpio" or
 *   "/dev/gpio0" on a board with more than one controller.
 *
 * @retval 0 Successful operation.
 * @retval -1 Failed, with errno set.  The controller's destroy handler has
 *   been called.
 */
int rtems_gpio_ctrl_register( rtems_gpio_ctrl *ctrl, const char *path );

/** @} */

/**
 * @name Application side
 *
 * Each of these is one ioctl() on @a fd, which is a descriptor for a
 * controller's node.  Each returns 0 on success, or -1 with errno set.
 *
 * errno is ENOTSUP where the driver does not implement the operation,
 * ENODEV where the pin is out of range, EBUSY where the pin is already
 * configured by someone else, EACCES where the board reserves the pin, and
 * ENOTTY where @a fd is not a GPIO controller.
 *
 * @{
 */

/**
 * @brief Reports the controller behind @a fd.
 */
int rtems_gpio_get_info( int fd, rtems_gpio_info *info );

/**
 * @brief Reports what pin @a pin is, before anything is done to it.
 */
int rtems_gpio_pin_get_info(
  int                  fd,
  uint32_t             pin,
  rtems_gpio_pin_info *info
);

/**
 * @brief Finds the pin the board calls @a name.
 *
 * Walks the controller's pins and returns the first whose
 * rtems_gpio_pin_info::name matches.  Linear, and meant for start-up rather
 * than for a loop.
 *
 * @param[out] pin The logical pin, if it was found.
 *
 * @retval 0 Found.
 * @retval -1 Failed, with errno set to ENOENT if no pin has that name.
 */
int rtems_gpio_pin_by_name( int fd, const char *name, uint32_t *pin );

/**
 * @brief Configures a pin.
 *
 * Refused with EACCES if the board reserves the pin, EBUSY if it is already
 * configured, and ENOTSUP if the configuration asks for something the pin
 * does not report in its capabilities.  None of those changes the hardware.
 *
 * @param[in,out] config What to apply.  On success it holds what was
 *   actually applied, which may differ where the driver rounded a value.
 */
int rtems_gpio_pin_configure(
  int                fd,
  uint32_t           pin,
  rtems_gpio_config *config
);

/**
 * @brief Reports how a pin is configured now.
 *
 * For a pin nothing has configured, the direction is
 * RTEMS_GPIO_DIRECTION_NONE and the rest of the structure is zero.
 */
int rtems_gpio_pin_get_configuration(
  int                fd,
  uint32_t           pin,
  rtems_gpio_config *config
);

/**
 * @brief Returns a pin to its unconfigured state.
 */
int rtems_gpio_pin_release( int fd, uint32_t pin );

/**
 * @brief Reads a pin.
 *
 * @param[out] value 0 or 1, logical: a pin configured
 *   RTEMS_GPIO_FLAG_ACTIVE_LOW reads 1 when the pad is low.
 */
int rtems_gpio_pin_get( int fd, uint32_t pin, int *value );

/**
 * @brief Writes a pin.
 */
int rtems_gpio_pin_set( int fd, uint32_t pin, int value );

/**
 * @brief Inverts a pin.
 */
int rtems_gpio_pin_toggle( int fd, uint32_t pin );

/**
 * @brief Reads several pins as one operation.
 *
 * @param[in,out] list The pins to read on the way in, their levels in
 *   rtems_gpio_pin_list::values on the way out.
 */
int rtems_gpio_pin_get_multiple( int fd, rtems_gpio_pin_list *list );

/**
 * @brief Writes several pins as one operation.
 */
int rtems_gpio_pin_set_multiple( int fd, const rtems_gpio_pin_list *list );

/**
 * @brief Starts delivering a pin's interrupt to @a handler.
 *
 * The pin must already be configured with a trigger other than
 * RTEMS_GPIO_TRIGGER_NONE.
 */
int rtems_gpio_pin_irq_enable(
  int                    fd,
  uint32_t               pin,
  rtems_gpio_irq_handler handler,
  void                  *arg
);

/**
 * @brief Stops delivering a pin's interrupt.
 */
int rtems_gpio_pin_irq_disable( int fd, uint32_t pin );

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _DEV_GPIO_GPIO_H */
