/* SPDX-License-Identifier: BSD-2-Clause */

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

#include <dev/gpio/gpio-virtual.h>

#include <rtems/libcsupport.h>
#include <tmacros.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

const char rtems_test_name[] = "GPIO 2";

#define VIRT_PATH "/dev/gpio-virtual"
#define BACK_PATH "/dev/gpio-backing"
#define SPARE_PATH "/dev/gpio-spare"

/*
 * Two controllers, because the case this driver exists for is a pin that
 * is not a pad: the proxy pin of the first forwards to a pin of the
 * second through the ordinary API, which is the only way a callback may
 * reach a GPIO at all -- the controller mutex is held across a callback,
 * so a callback that came back to its own controller would deadlock.
 */

/* Pins of the virtual controller. */

/** Forwards to a pin of the backing controller. */
#define VIRT_PROXY 0
/** A clock gate: it can be driven and not read. */
#define VIRT_GATE 1
/** A readiness line: it can be read and not driven. */
#define VIRT_SENSE 2
/** A software signal, readable, writable and reading back. */
#define VIRT_SIGNAL 3
/** A subsystem enable the board does not hand out. */
#define VIRT_RESERVED 4
/** Nothing but an interrupt: neither callback. */
#define VIRT_TRIGGER 5
/** A signal whose set callback raises its own interrupt. */
#define VIRT_NOTIFY 6
/** An entry left zero: a pin that exists and can do nothing. */
#define VIRT_NOTHING 7
/** A pin in the second bitmap word, so a bulk operation spans two. */
#define VIRT_HIGH 33

#define VIRT_PIN_COUNT 40

#define BACK_PIN 0
#define BACK_PIN_COUNT 1

#define VIRT_WORDS RTEMS_GPIO_BITMAP_WORDS(VIRT_PIN_COUNT)

/*
 * What sits behind a software pin.  It counts the calls, so that a test
 * can tell a value the driver read from the callback from one it produced
 * out of its own shadow, and it can be made to fail once, which is how
 * the paths that report a callback's errno are reached.
 */
typedef struct {
  int level; /* physical, never logical */
  int gets;
  int sets;
  int fail_get;
  int fail_set;
} virt_soft;

static virt_soft soft_gate;
static virt_soft soft_sense;
static virt_soft soft_signal;
static virt_soft soft_reserved;
static virt_soft soft_notify;
static virt_soft soft_high;
static virt_soft soft_back;
static virt_soft soft_spare;

static rtems_gpio_virtual_ctrl virt_ctrl;
static rtems_gpio_virtual_ctrl back_ctrl;
static rtems_gpio_virtual_ctrl spare_ctrl;

static int back_fd = -1;

static int soft_get(uint32_t pin, void* arg, int* value) {
  virt_soft* soft = arg;

  (void)pin;
  ++soft->gets;

  if (soft->fail_get != 0) {
    int err = soft->fail_get;

    soft->fail_get = 0;

    return err;
  }

  *value = soft->level;

  return 0;
}

static int soft_set(uint32_t pin, void* arg, int value) {
  virt_soft* soft = arg;

  (void)pin;
  ++soft->sets;

  if (soft->fail_set != 0) {
    int err = soft->fail_set;

    soft->fail_set = 0;

    return err;
  }

  soft->level = value;

  return 0;
}

/*
 * The proxy.  It speaks to the backing controller through the same calls
 * an application would, which is what makes it a proxy rather than a
 * second copy of the thing behind the pin.  The level is logical there
 * and physical here, and they are the same number only because nothing
 * configured the backing pin active low.
 */
static int proxy_get(uint32_t pin, void* arg, int* value) {
  (void)pin;

  if (rtems_gpio_pin_get(*(const int*)arg, BACK_PIN, value) != 0) {
    return errno;
  }

  return 0;
}

static int proxy_set(uint32_t pin, void* arg, int value) {
  (void)pin;

  if (rtems_gpio_pin_set(*(const int*)arg, BACK_PIN, value) != 0) {
    return errno;
  }

  return 0;
}

static int notify_raises;

static void notify_handler(uint32_t pin, void* arg) {
  (void)pin;
  (void)arg;

  ++notify_raises;
}

/*
 * A pin whose write is also the event: the task that drives it does not
 * have to tell anyone, because the set callback raises the interrupt.
 * The handler then runs under the controller mutex, so it counts and
 * touches nothing else of this controller.
 */
static int notify_set(uint32_t pin, void* arg, int value) {
  int err = soft_set(pin, arg, value);

  if (err == 0 && value != 0) {
    err = rtems_gpio_virtual_raise(&virt_ctrl, pin);

    /* Nobody listening is not a failure of the write. */
    if (err == ENOENT) {
      err = 0;
    }
  }

  return err;
}

#define VIRT_CAPS_BOTH                                                         \
  (RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_OUTPUT |                              \
   RTEMS_GPIO_CAP_OUTPUT_READBACK)

static const rtems_gpio_virtual_pin virt_pins[VIRT_PIN_COUNT] = {
    [VIRT_PROXY] = {.get = proxy_get,
                    .set = proxy_set,
                    .arg = &back_fd,
                    .capabilities = VIRT_CAPS_BOTH | RTEMS_GPIO_CAP_EDGE_BOTH,
                    .name = "EXP0"},
    /*
     * No get callback, so no RTEMS_GPIO_CAP_INPUT, so an input
     * configuration is refused by the generic layer and not by anything
     * in the driver.
     */
    [VIRT_GATE] = {.set = soft_set,
                   .arg = &soft_gate,
                   .capabilities = RTEMS_GPIO_CAP_OUTPUT,
                   .name = "CLK_EN"},
    [VIRT_SENSE] = {.get = soft_get,
                    .arg = &soft_sense,
                    .capabilities =
                        RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_EDGE_RISING,
                    .name = "WIFI_READY"},
    [VIRT_SIGNAL] = {.get = soft_get,
                     .set = soft_set,
                     .arg = &soft_signal,
                     .capabilities = VIRT_CAPS_BOTH |
                                     RTEMS_GPIO_CAP_EDGE_RISING |
                                     RTEMS_GPIO_CAP_EDGE_FALLING,
                     .name = "SIG"},
    [VIRT_RESERVED] = {.get = soft_get,
                       .set = soft_set,
                       .arg = &soft_reserved,
                       .capabilities =
                           RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_OUTPUT,
                       .flags = RTEMS_GPIO_PIN_RESERVED,
                       .name = "BT_EN",
                       .owner = "bt-radio"},
    [VIRT_TRIGGER] = {.capabilities = RTEMS_GPIO_CAP_EDGE_RISING,
                      .name = "DOORBELL"},
    [VIRT_NOTIFY] = {.set = notify_set,
                     .arg = &soft_notify,
                     .capabilities =
                         RTEMS_GPIO_CAP_OUTPUT | RTEMS_GPIO_CAP_EDGE_RISING,
                     .name = "NOTIFY"},
    [VIRT_HIGH] = {.get = soft_get,
                   .set = soft_set,
                   .arg = &soft_high,
                   .capabilities = VIRT_CAPS_BOTH,
                   .name = "HIGH33"}};

static const rtems_gpio_virtual_pin back_pins[BACK_PIN_COUNT] = {
    [BACK_PIN] = {.get = soft_get,
                  .set = soft_set,
                  .arg = &soft_back,
                  .capabilities = VIRT_CAPS_BOTH,
                  .name = "EXP_PAD"}};

RTEMS_GPIO_VIRTUAL_STORAGE_DEFINE(virt, VIRT_PIN_COUNT)
RTEMS_GPIO_VIRTUAL_STORAGE_DEFINE(back, BACK_PIN_COUNT)
RTEMS_GPIO_VIRTUAL_STORAGE_DEFINE(spare, 1)

static void assert_fails(int rv, int expected_errno) {
  rtems_test_assert(rv == -1);
  rtems_test_assert(errno == expected_errno);
}

static void configure(int fd, uint32_t pin, rtems_gpio_direction direction,
                      rtems_gpio_trigger trigger, uint32_t flags,
                      int initial_value) {
  rtems_gpio_config config;

  memset(&config, 0, sizeof(config));
  config.direction = direction;
  config.trigger = trigger;
  config.flags = flags;
  config.initial_value = initial_value;

  rtems_test_assert(rtems_gpio_pin_configure(fd, pin, &config) == 0);
}

static void expect_register_error(const rtems_gpio_virtual_config* config,
                                  int expected) {
  rtems_test_assert(
      rtems_gpio_virtual_register(&spare_ctrl, config, SPARE_PATH) == expected);
}

/*
 * A table that says a pin can do something no callback implements is a
 * board description that is wrong, and it is refused here rather than
 * quietly corrected: a pin reported without the capability the board
 * claims for it fails much later, in whatever tried to use it.
 */
static void test_registration_is_checked(void) {
  static const rtems_gpio_virtual_pin bad_unsupported[1] = {
      {.capabilities = RTEMS_GPIO_CAP_DEBOUNCE}};
  static const rtems_gpio_virtual_pin bad_output[1] = {
      {.capabilities = RTEMS_GPIO_CAP_OUTPUT}};
  static const rtems_gpio_virtual_pin bad_input[1] = {
      {.capabilities = RTEMS_GPIO_CAP_INPUT}};
  static const rtems_gpio_virtual_pin bad_readback[1] = {
      {.set = soft_set,
       .arg = &soft_spare,
       .capabilities = RTEMS_GPIO_CAP_OUTPUT | RTEMS_GPIO_CAP_OUTPUT_READBACK}};
  static const rtems_gpio_virtual_pin bad_readback_only[1] = {
      {.get = soft_get,
       .arg = &soft_spare,
       .capabilities = RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_OUTPUT_READBACK}};
  static const rtems_gpio_virtual_pin good[1] = {
      {.get = soft_get,
       .arg = &soft_spare,
       .capabilities = RTEMS_GPIO_CAP_INPUT}};
  rtems_gpio_virtual_config config;

  /*
   * Filled in by hand rather than with RTEMS_GPIO_VIRTUAL_STORAGE(),
   * because each case below takes one member away again.
   */
  memset(&config, 0, sizeof(config));
  config.name = "gpio-spare";
  config.pins = good;
  config.pin_count = 1;
  config.state = spare_state;
  config.active_low = spare_active_low;
  config.scratch = spare_scratch;

  rtems_test_assert(rtems_gpio_virtual_register(NULL, &config, SPARE_PATH) ==
                    EINVAL);
  rtems_test_assert(
      rtems_gpio_virtual_register(&spare_ctrl, NULL, SPARE_PATH) == EINVAL);
  rtems_test_assert(rtems_gpio_virtual_register(&spare_ctrl, &config, NULL) ==
                    EINVAL);

  config.pins = NULL;
  expect_register_error(&config, EINVAL);
  config.pins = good;

  config.state = NULL;
  expect_register_error(&config, EINVAL);
  config.state = spare_state;

  /* The pin count and the bitmaps are the generic layer's to refuse. */
  config.pin_count = 0;
  expect_register_error(&config, EINVAL);
  config.pin_count = 1;

  config.active_low = NULL;
  expect_register_error(&config, EINVAL);
  config.active_low = spare_active_low;

  config.pins = bad_unsupported;
  expect_register_error(&config, EINVAL);

  config.pins = bad_output;
  expect_register_error(&config, EINVAL);

  config.pins = bad_input;
  expect_register_error(&config, EINVAL);

  config.pins = bad_readback;
  expect_register_error(&config, EINVAL);

  config.pins = bad_readback_only;
  expect_register_error(&config, EINVAL);

  /* And the same table, registered twice, is the second node failing. */
  config.pins = good;
  rtems_test_assert(
      rtems_gpio_virtual_register(&spare_ctrl, &config, SPARE_PATH) == 0);
  rtems_test_assert(
      rtems_gpio_virtual_register(&spare_ctrl, &config, SPARE_PATH) != 0);
  rtems_test_assert(unlink(SPARE_PATH) == 0);
}

/*
 * Whether a call may block is the registrant's answer and not the
 * driver's.  The proxy reaches another controller and can wait on its
 * mutex; the backing controller is a variable in memory and cannot.
 */
static void test_controller_info(int virt, int back) {
  rtems_gpio_info info;

  rtems_test_assert(rtems_gpio_get_info(virt, &info) == 0);
  rtems_test_assert(info.pin_count == VIRT_PIN_COUNT);
  rtems_test_assert(strcmp(info.name, "gpio-virtual") == 0);
  rtems_test_assert(info.can_block);

  rtems_test_assert(rtems_gpio_get_info(back, &info) == 0);
  rtems_test_assert(info.pin_count == BACK_PIN_COUNT);
  rtems_test_assert(!info.can_block);
}

static void test_pin_info(int fd) {
  rtems_gpio_pin_info info;
  uint32_t pin;

  /* Every pin here is one the driver provides, whatever is behind it. */
  rtems_test_assert(rtems_gpio_pin_get_info(fd, VIRT_PROXY, &info) == 0);
  rtems_test_assert(info.kind == RTEMS_GPIO_PIN_VIRTUAL);
  rtems_test_assert(info.flags == RTEMS_GPIO_PIN_AVAILABLE);
  rtems_test_assert(strcmp(info.name, "EXP0") == 0);
  rtems_test_assert(info.owner[0] == '\0');

  /* Capabilities are per pin: this one drives and does not read. */
  rtems_test_assert(rtems_gpio_pin_get_info(fd, VIRT_GATE, &info) == 0);
  rtems_test_assert(info.capabilities == RTEMS_GPIO_CAP_OUTPUT);

  /* And this one reads and does not drive, and interrupts as well. */
  rtems_test_assert(rtems_gpio_pin_get_info(fd, VIRT_SENSE, &info) == 0);
  rtems_test_assert(info.capabilities ==
                    (RTEMS_GPIO_CAP_INPUT | RTEMS_GPIO_CAP_EDGE_RISING));

  rtems_test_assert(rtems_gpio_pin_get_info(fd, VIRT_RESERVED, &info) == 0);
  rtems_test_assert((info.flags & RTEMS_GPIO_PIN_RESERVED) != 0);
  rtems_test_assert(strcmp(info.owner, "bt-radio") == 0);

  rtems_test_assert(rtems_gpio_pin_get_info(fd, VIRT_NOTHING, &info) == 0);
  rtems_test_assert(info.capabilities == 0);
  rtems_test_assert(info.name[0] == '\0');

  rtems_test_assert(rtems_gpio_pin_by_name(fd, "HIGH33", &pin) == 0);
  rtems_test_assert(pin == VIRT_HIGH);
}

/*
 * The refusal that matters for a table of callbacks: a pin with no set is
 * an input, and the generic layer refuses an output on it from the
 * capability the pin did not report.  Nothing in the driver has a special
 * case for it.
 */
static void test_direction_is_refused(int fd) {
  rtems_gpio_config config;
  rtems_gpio_pin_info info;

  memset(&config, 0, sizeof(config));
  config.direction = RTEMS_GPIO_DIRECTION_OUTPUT;
  assert_fails(rtems_gpio_pin_configure(fd, VIRT_SENSE, &config), ENOTSUP);

  rtems_test_assert(rtems_gpio_pin_get_info(fd, VIRT_SENSE, &info) == 0);
  rtems_test_assert((info.flags & RTEMS_GPIO_PIN_IN_USE) == 0);

  /* The other way round on the pin that has only a set. */
  memset(&config, 0, sizeof(config));
  config.direction = RTEMS_GPIO_DIRECTION_INPUT;
  assert_fails(rtems_gpio_pin_configure(fd, VIRT_GATE, &config), ENOTSUP);

  /* A pin the board keeps for itself is not handed out at all. */
  config.direction = RTEMS_GPIO_DIRECTION_OUTPUT;
  assert_fails(rtems_gpio_pin_configure(fd, VIRT_RESERVED, &config), EACCES);

  /* A pin with neither callback still takes an interrupt configuration. */
  configure(fd, VIRT_TRIGGER, RTEMS_GPIO_DIRECTION_NONE,
            RTEMS_GPIO_TRIGGER_EDGE_RISING, 0, 0);
  rtems_test_assert(rtems_gpio_pin_get_info(fd, VIRT_TRIGGER, &info) == 0);
  rtems_test_assert((info.flags & RTEMS_GPIO_PIN_IN_USE) != 0);

  /* ... and cannot be read or driven, since nothing implements either. */
  assert_fails(rtems_gpio_pin_get(fd, VIRT_TRIGGER, &config.initial_value),
               ENOTSUP);
  assert_fails(rtems_gpio_pin_set(fd, VIRT_TRIGGER, 1), ENOTSUP);
  assert_fails(rtems_gpio_pin_toggle(fd, VIRT_TRIGGER), ENOTSUP);

  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_TRIGGER) == 0);
}

/*
 * The callbacks are what the pin is, so the test asserts against them
 * rather than against a second call of the API.
 */
static void test_callbacks_are_called(int fd) {
  rtems_gpio_config config;
  int value;

  memset(&soft_gate, 0, sizeof(soft_gate));

  /* The level goes on as part of configuring, not after it. */
  configure(fd, VIRT_GATE, RTEMS_GPIO_DIRECTION_OUTPUT, RTEMS_GPIO_TRIGGER_NONE,
            0, 1);
  rtems_test_assert(soft_gate.sets == 1);
  rtems_test_assert(soft_gate.level == 1);

  rtems_test_assert(rtems_gpio_pin_set(fd, VIRT_GATE, 0) == 0);
  rtems_test_assert(soft_gate.sets == 2);
  rtems_test_assert(soft_gate.level == 0);

  rtems_test_assert(rtems_gpio_pin_toggle(fd, VIRT_GATE) == 0);
  rtems_test_assert(soft_gate.sets == 3);
  rtems_test_assert(soft_gate.level == 1);

  /*
   * It has no get callback and does not report a readback, so reading it
   * returns the last level written and asks nothing.  That is what lets a
   * clock gate be a write-only thing and still be readable.
   */
  rtems_test_assert(rtems_gpio_pin_get(fd, VIRT_GATE, &value) == 0);
  rtems_test_assert(value == 1);
  rtems_test_assert(soft_gate.gets == 0);

  /* What the driver reports is what it applied. */
  rtems_test_assert(rtems_gpio_pin_get_configuration(fd, VIRT_GATE, &config) ==
                    0);
  rtems_test_assert(config.direction == RTEMS_GPIO_DIRECTION_OUTPUT);

  /* A callback that refuses is the caller's error, unchanged. */
  soft_gate.fail_set = EIO;
  assert_fails(rtems_gpio_pin_set(fd, VIRT_GATE, 0), EIO);
  rtems_test_assert(soft_gate.level == 1);

  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_GATE) == 0);

  /* Released, so the generic layer will not let it be driven again. */
  assert_fails(rtems_gpio_pin_set(fd, VIRT_GATE, 1), EBADF);

  /* And a configure that the callback refuses leaves it unconfigured. */
  soft_gate.fail_set = EIO;
  memset(&config, 0, sizeof(config));
  config.direction = RTEMS_GPIO_DIRECTION_OUTPUT;
  assert_fails(rtems_gpio_pin_configure(fd, VIRT_GATE, &config), EIO);
  assert_fails(rtems_gpio_pin_get(fd, VIRT_GATE, &value), EBADF);

  /* The input half of the same argument. */
  memset(&soft_sense, 0, sizeof(soft_sense));
  configure(fd, VIRT_SENSE, RTEMS_GPIO_DIRECTION_INPUT, RTEMS_GPIO_TRIGGER_NONE,
            0, 0);
  soft_sense.level = 1;
  rtems_test_assert(rtems_gpio_pin_get(fd, VIRT_SENSE, &value) == 0);
  rtems_test_assert(value == 1);
  rtems_test_assert(soft_sense.gets == 1);

  soft_sense.fail_get = EIO;
  assert_fails(rtems_gpio_pin_get(fd, VIRT_SENSE, &value), EIO);

  /* An input cannot be driven, whatever the caller thinks. */
  assert_fails(rtems_gpio_pin_set(fd, VIRT_SENSE, 1), ENOTSUP);

  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_SENSE) == 0);
}

/*
 * A pin that reports RTEMS_GPIO_CAP_OUTPUT_READBACK is asked its callback
 * even when it is an output, so a change made behind the driver's back is
 * seen.  The gate above, which does not report it, would not have seen
 * one.
 */
static void test_readback(int fd) {
  int value;

  memset(&soft_signal, 0, sizeof(soft_signal));

  configure(fd, VIRT_SIGNAL, RTEMS_GPIO_DIRECTION_OUTPUT,
            RTEMS_GPIO_TRIGGER_NONE, 0, 0);
  soft_signal.level = 1;

  rtems_test_assert(rtems_gpio_pin_get(fd, VIRT_SIGNAL, &value) == 0);
  rtems_test_assert(value == 1);
  rtems_test_assert(soft_signal.gets == 1);

  /* So a toggle of such a pin is against the level it actually has. */
  rtems_test_assert(rtems_gpio_pin_toggle(fd, VIRT_SIGNAL) == 0);
  rtems_test_assert(soft_signal.level == 0);

  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_SIGNAL) == 0);
}

/* The pin that is another controller's pin. */
static void test_proxy(int virt, int back) {
  int value;

  memset(&soft_back, 0, sizeof(soft_back));

  configure(back, BACK_PIN, RTEMS_GPIO_DIRECTION_OUTPUT,
            RTEMS_GPIO_TRIGGER_NONE, 0, 0);
  configure(virt, VIRT_PROXY, RTEMS_GPIO_DIRECTION_OUTPUT,
            RTEMS_GPIO_TRIGGER_NONE, 0, 0);

  rtems_test_assert(rtems_gpio_pin_set(virt, VIRT_PROXY, 1) == 0);

  /* It arrived at the far side, and by the far side's own account. */
  rtems_test_assert(soft_back.level == 1);
  rtems_test_assert(rtems_gpio_pin_get(back, BACK_PIN, &value) == 0);
  rtems_test_assert(value == 1);

  /* And a change made at the far side is seen through the proxy. */
  soft_back.level = 0;
  rtems_test_assert(rtems_gpio_pin_get(virt, VIRT_PROXY, &value) == 0);
  rtems_test_assert(value == 0);

  /* A refusal from the far side reaches the caller of the near side. */
  soft_back.fail_set = EIO;
  assert_fails(rtems_gpio_pin_set(virt, VIRT_PROXY, 1), EIO);
  soft_back.fail_get = EIO;
  assert_fails(rtems_gpio_pin_get(virt, VIRT_PROXY, &value), EIO);

  rtems_test_assert(rtems_gpio_pin_release(virt, VIRT_PROXY) == 0);
  rtems_test_assert(rtems_gpio_pin_release(back, BACK_PIN) == 0);
}

/*
 * Polarity is the generic layer's and the callback never sees it.  A
 * driver that applied it too would invert twice and nothing above would
 * be able to tell.
 */
static void test_active_low(int fd) {
  int value;

  memset(&soft_signal, 0, sizeof(soft_signal));

  /* Logical 1 on configuring, so the callback is given physical 0. */
  configure(fd, VIRT_SIGNAL, RTEMS_GPIO_DIRECTION_OUTPUT,
            RTEMS_GPIO_TRIGGER_NONE, RTEMS_GPIO_FLAG_ACTIVE_LOW, 1);
  rtems_test_assert(soft_signal.level == 0);

  rtems_test_assert(rtems_gpio_pin_set(fd, VIRT_SIGNAL, 0) == 0);
  rtems_test_assert(soft_signal.level == 1);

  /* And back to logical on the way up. */
  rtems_test_assert(rtems_gpio_pin_get(fd, VIRT_SIGNAL, &value) == 0);
  rtems_test_assert(value == 0);

  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_SIGNAL) == 0);
}

static void test_bulk(int fd) {
  rtems_gpio_pin_bitmap map;
  uint32_t mask[VIRT_WORDS];
  uint32_t values[VIRT_WORDS];

  memset(&soft_gate, 0, sizeof(soft_gate));
  memset(&soft_signal, 0, sizeof(soft_signal));
  memset(&soft_high, 0, sizeof(soft_high));

  configure(fd, VIRT_GATE, RTEMS_GPIO_DIRECTION_OUTPUT, RTEMS_GPIO_TRIGGER_NONE,
            0, 0);
  configure(fd, VIRT_SIGNAL, RTEMS_GPIO_DIRECTION_OUTPUT,
            RTEMS_GPIO_TRIGGER_NONE, 0, 0);
  configure(fd, VIRT_HIGH, RTEMS_GPIO_DIRECTION_OUTPUT, RTEMS_GPIO_TRIGGER_NONE,
            0, 0);
  configure(fd, VIRT_SENSE, RTEMS_GPIO_DIRECTION_INPUT, RTEMS_GPIO_TRIGGER_NONE,
            0, 0);
  configure(fd, VIRT_TRIGGER, RTEMS_GPIO_DIRECTION_NONE,
            RTEMS_GPIO_TRIGGER_EDGE_RISING, 0, 0);

  memset(mask, 0, sizeof(mask));
  memset(values, 0, sizeof(values));
  mask[0] = (1u << VIRT_GATE) | (1u << VIRT_SIGNAL);
  mask[1] = 1u << (VIRT_HIGH - RTEMS_GPIO_BITMAP_WORD_BITS);
  values[0] = 1u << VIRT_GATE;
  values[1] = mask[1];

  map.word_count = VIRT_WORDS;
  map.mask = mask;
  map.values = values;

  rtems_test_assert(rtems_gpio_pin_set_multiple(fd, &map) == 0);
  rtems_test_assert(soft_gate.level == 1);
  rtems_test_assert(soft_signal.level == 0);
  rtems_test_assert(soft_high.level == 1);

  /* Read the same three back, across the word boundary. */
  memset(values, 0, sizeof(values));
  rtems_test_assert(rtems_gpio_pin_get_multiple(fd, &map) == 0);
  rtems_test_assert((values[0] & (1u << VIRT_GATE)) != 0);
  rtems_test_assert((values[0] & (1u << VIRT_SIGNAL)) == 0);
  rtems_test_assert(values[1] == mask[1]);

  /*
   * One word of bitmap for a controller of two.  The second word is the
   * caller's and this operation was never told it exists, so a driver
   * that walked its own pin count instead of the caller's would write
   * past the end of it.
   */
  values[1] = 0xdeadbeef;
  soft_high.gets = 0;
  soft_high.sets = 0;
  map.word_count = 1;
  rtems_test_assert(rtems_gpio_pin_get_multiple(fd, &map) == 0);
  rtems_test_assert(values[1] == 0xdeadbeef);
  rtems_test_assert(rtems_gpio_pin_set_multiple(fd, &map) == 0);

  /*
   * Asserted against the pin and not only against the bitmap, because a
   * bit that happens to already hold the level the pin is at would make
   * an out of range write look like no write at all.
   */
  rtems_test_assert(soft_high.gets == 0);
  rtems_test_assert(soft_high.sets == 0);
  rtems_test_assert(soft_high.level == 1);
  map.word_count = VIRT_WORDS;

  /*
   * A write naming one pin that cannot be written changes none of them,
   * which is decided before any callback runs.
   */
  memset(values, 0, sizeof(values));
  mask[0] = (1u << VIRT_GATE) | (1u << VIRT_SENSE);
  mask[1] = 0;
  soft_gate.sets = 0;
  assert_fails(rtems_gpio_pin_set_multiple(fd, &map), ENOTSUP);
  rtems_test_assert(soft_gate.sets == 0);
  rtems_test_assert(soft_gate.level == 1);

  /* And the same for a read naming a pin that cannot be read. */
  mask[0] = (1u << VIRT_GATE) | (1u << VIRT_TRIGGER);
  soft_gate.gets = 0;
  assert_fails(rtems_gpio_pin_get_multiple(fd, &map), ENOTSUP);

  /* A callback that fails part way through is the errno it gave. */
  mask[0] = 1u << VIRT_SIGNAL;
  mask[1] = 1u << (VIRT_HIGH - RTEMS_GPIO_BITMAP_WORD_BITS);
  soft_high.fail_set = EIO;
  assert_fails(rtems_gpio_pin_set_multiple(fd, &map), EIO);
  soft_high.fail_get = EIO;
  assert_fails(rtems_gpio_pin_get_multiple(fd, &map), EIO);

  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_GATE) == 0);
  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_SIGNAL) == 0);
  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_HIGH) == 0);
  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_SENSE) == 0);
  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_TRIGGER) == 0);
}

static uint32_t irq_pin;
static void* irq_arg;
static int irq_count;

static void irq_handler(uint32_t pin, void* arg) {
  irq_pin = pin;
  irq_arg = arg;
  ++irq_count;
}

/*
 * The case the raise exists for: something the board owns becomes ready
 * and the consumer finds out through the pin it already has, rather than
 * through an interface of the BSP's own.
 */
static void test_interrupts(int fd) {
  int token = 0;

  irq_count = 0;

  /* Out of range, absent and unregistered, before anything is enabled. */
  rtems_test_assert(rtems_gpio_virtual_raise(NULL, VIRT_SENSE) == EINVAL);
  rtems_test_assert(rtems_gpio_virtual_raise(&virt_ctrl, VIRT_PIN_COUNT) ==
                    ENODEV);
  rtems_test_assert(rtems_gpio_virtual_raise(&virt_ctrl, VIRT_SENSE) == ENOENT);

  configure(fd, VIRT_SENSE, RTEMS_GPIO_DIRECTION_INPUT,
            RTEMS_GPIO_TRIGGER_EDGE_RISING, 0, 0);
  rtems_test_assert(
      rtems_gpio_pin_irq_enable(fd, VIRT_SENSE, irq_handler, &token) == 0);

  rtems_test_assert(rtems_gpio_virtual_raise(&virt_ctrl, VIRT_SENSE) == 0);
  rtems_test_assert(irq_count == 1);
  rtems_test_assert(irq_pin == VIRT_SENSE);
  rtems_test_assert(irq_arg == &token);

  rtems_test_assert(rtems_gpio_pin_irq_disable(fd, VIRT_SENSE) == 0);
  rtems_test_assert(rtems_gpio_virtual_raise(&virt_ctrl, VIRT_SENSE) == ENOENT);
  rtems_test_assert(irq_count == 1);

  /* Releasing the pin takes the handler with it. */
  rtems_test_assert(
      rtems_gpio_pin_irq_enable(fd, VIRT_SENSE, irq_handler, &token) == 0);
  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_SENSE) == 0);
  rtems_test_assert(rtems_gpio_virtual_raise(&virt_ctrl, VIRT_SENSE) == ENOENT);

  /* A pin configured with no trigger has no interrupt to deliver. */
  memset(&soft_gate, 0, sizeof(soft_gate));
  configure(fd, VIRT_GATE, RTEMS_GPIO_DIRECTION_OUTPUT, RTEMS_GPIO_TRIGGER_NONE,
            0, 0);
  assert_fails(rtems_gpio_pin_irq_enable(fd, VIRT_GATE, irq_handler, &token),
               EINVAL);
  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_GATE) == 0);

  /* A pin that is nothing but an interrupt still delivers one. */
  configure(fd, VIRT_TRIGGER, RTEMS_GPIO_DIRECTION_NONE,
            RTEMS_GPIO_TRIGGER_EDGE_RISING, 0, 0);
  rtems_test_assert(
      rtems_gpio_pin_irq_enable(fd, VIRT_TRIGGER, irq_handler, NULL) == 0);
  rtems_test_assert(rtems_gpio_virtual_raise(&virt_ctrl, VIRT_TRIGGER) == 0);
  rtems_test_assert(irq_count == 2);
  rtems_test_assert(irq_pin == VIRT_TRIGGER);
  rtems_test_assert(irq_arg == NULL);
  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_TRIGGER) == 0);
}

/*
 * Raising from inside a set callback, which is the shape a software
 * signal takes: one task writes the pin and the write is the event.  The
 * handler runs under the controller mutex the write already holds, so it
 * may not come back here, and this one does not.
 */
static void test_raise_from_callback(int fd) {
  memset(&soft_notify, 0, sizeof(soft_notify));
  notify_raises = 0;

  /* Nothing enabled yet, so the write reports the raise went nowhere. */
  configure(fd, VIRT_NOTIFY, RTEMS_GPIO_DIRECTION_OUTPUT,
            RTEMS_GPIO_TRIGGER_EDGE_RISING, 0, 1);
  rtems_test_assert(notify_raises == 0);

  rtems_test_assert(
      rtems_gpio_pin_irq_enable(fd, VIRT_NOTIFY, notify_handler, NULL) == 0);

  rtems_test_assert(rtems_gpio_pin_set(fd, VIRT_NOTIFY, 0) == 0);
  rtems_test_assert(notify_raises == 0);

  rtems_test_assert(rtems_gpio_pin_set(fd, VIRT_NOTIFY, 1) == 0);
  rtems_test_assert(notify_raises == 1);
  rtems_test_assert(soft_notify.level == 1);

  rtems_test_assert(rtems_gpio_pin_release(fd, VIRT_NOTIFY) == 0);
}

static int register_controllers(void) {
  static const rtems_gpio_virtual_config back_config = {
      .name = "gpio-backing",
      .pins = back_pins,
      RTEMS_GPIO_VIRTUAL_STORAGE(back, BACK_PIN_COUNT),
      /* A variable in memory: nothing here waits for anything. */
      .can_block = false};
  static const rtems_gpio_virtual_config virt_config = {
      .name = "gpio-virtual",
      .pins = virt_pins,
      RTEMS_GPIO_VIRTUAL_STORAGE(virt, VIRT_PIN_COUNT),
      /* Its proxy pin takes another controller's mutex, so it may wait. */
      .can_block = true};
  int err;

  err = rtems_gpio_virtual_register(&back_ctrl, &back_config, BACK_PATH);
  if (err != 0) {
    return err;
  }

  return rtems_gpio_virtual_register(&virt_ctrl, &virt_config, VIRT_PATH);
}

static void run_test(void) {
  rtems_resource_snapshot snapshot;
  int virt;
  int warm_up;

  rtems_test_assert(register_controllers() == 0);

  /*
   * Both nodes are opened and closed once before the baseline is taken.
   * The first open of an IMFS node allocates things it keeps
   * deliberately, and counting those as a leak reports a failure that is
   * not one.
   */
  warm_up = open(VIRT_PATH, O_RDWR);
  rtems_test_assert(warm_up >= 0);
  rtems_test_assert(close(warm_up) == 0);
  warm_up = open(BACK_PATH, O_RDWR);
  rtems_test_assert(warm_up >= 0);
  rtems_test_assert(close(warm_up) == 0);

  rtems_resource_snapshot_take(&snapshot);

  virt = open(VIRT_PATH, O_RDWR);
  rtems_test_assert(virt >= 0);
  back_fd = open(BACK_PATH, O_RDWR);
  rtems_test_assert(back_fd >= 0);

  test_registration_is_checked();
  test_controller_info(virt, back_fd);
  test_pin_info(virt);
  test_direction_is_refused(virt);
  test_callbacks_are_called(virt);
  test_readback(virt);
  test_proxy(virt, back_fd);
  test_active_low(virt);
  test_bulk(virt);
  test_interrupts(virt);
  test_raise_from_callback(virt);

  rtems_test_assert(close(back_fd) == 0);
  back_fd = -1;
  rtems_test_assert(close(virt) == 0);

  rtems_test_assert(rtems_resource_snapshot_check(&snapshot));

  /* After the snapshot: the nodes existed when it was taken. */
  rtems_test_assert(unlink(VIRT_PATH) == 0);
  rtems_test_assert(unlink(BACK_PATH) == 0);
  rtems_test_assert(open(VIRT_PATH, O_RDWR) == -1);
}

static rtems_task Init(rtems_task_argument arg) {
  (void)arg;

  TEST_BEGIN();

  run_test();

  TEST_END();
  rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_DOES_NOT_NEED_CLOCK_DRIVER

#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 8

#define CONFIGURE_MAXIMUM_TASKS 1

#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT

#include <rtems/confdefs.h>
