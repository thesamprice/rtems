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

#include "test_gpio.h"

#include <rtems/libcsupport.h>
#include <tmacros.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

const char rtems_test_name[] = "GPIO 1";

#define GPIO_PATH     "/dev/gpio"
#define GPIO_MIN_PATH "/dev/gpio-min"

/*
 * A failed call must set errno and must not have touched the hardware.  The
 * second half is the one worth asserting: a refusal that has already muxed
 * the pad is the failure this API exists to prevent, and it looks exactly
 * like a refusal that has not.
 */
static void assert_fails( int rv, int expected_errno )
{
  rtems_test_assert( rv == -1 );
  rtems_test_assert( errno == expected_errno );
}

static void test_controller_info( int fd )
{
  rtems_gpio_info info;

  rtems_test_assert( rtems_gpio_get_info( fd, &info ) == 0 );
  rtems_test_assert( info.pin_count == TEST_GPIO_PIN_COUNT );
  rtems_test_assert( strcmp( info.name, "test-gpio" ) == 0 );
}

static void test_pin_info( int fd )
{
  rtems_gpio_pin_info info;

  rtems_test_assert(
    rtems_gpio_pin_get_info( fd, TEST_GPIO_PIN_FULL, &info ) == 0
  );
  rtems_test_assert( info.pin == TEST_GPIO_PIN_FULL );
  rtems_test_assert( info.kind == RTEMS_GPIO_PIN_PHYSICAL );
  rtems_test_assert( info.flags == RTEMS_GPIO_PIN_AVAILABLE );
  rtems_test_assert( ( info.capabilities & RTEMS_GPIO_CAP_OUTPUT ) != 0 );
  rtems_test_assert( strcmp( info.name, "LED0" ) == 0 );

  /* Reserved, and capable of everything, which is the point of it. */
  rtems_test_assert(
    rtems_gpio_pin_get_info( fd, TEST_GPIO_PIN_RESERVED, &info ) == 0
  );
  rtems_test_assert( ( info.flags & RTEMS_GPIO_PIN_RESERVED ) != 0 );
  rtems_test_assert( ( info.capabilities & RTEMS_GPIO_CAP_OUTPUT ) != 0 );

  rtems_test_assert(
    rtems_gpio_pin_get_info( fd, TEST_GPIO_PIN_STRAPPING, &info ) == 0
  );
  rtems_test_assert( ( info.flags & RTEMS_GPIO_PIN_STRAPPING ) != 0 );

  /* Virtual pins report themselves so a caller can tell a bus from a bit. */
  rtems_test_assert(
    rtems_gpio_pin_get_info( fd, TEST_GPIO_PIN_VIRTUAL, &info ) == 0
  );
  rtems_test_assert( info.kind == RTEMS_GPIO_PIN_VIRTUAL );
  rtems_test_assert( ( info.capabilities & RTEMS_GPIO_CAP_OPEN_DRAIN ) == 0 );

  /* One past the end is not a pin. */
  assert_fails(
    rtems_gpio_pin_get_info( fd, TEST_GPIO_PIN_COUNT, &info ),
    ENODEV
  );
}

static void test_pin_by_name( int fd )
{
  uint32_t pin;

  rtems_test_assert( rtems_gpio_pin_by_name( fd, "BOOT", &pin ) == 0 );
  rtems_test_assert( pin == TEST_GPIO_PIN_STRAPPING );

  rtems_test_assert( rtems_gpio_pin_by_name( fd, "LED0", &pin ) == 0 );
  rtems_test_assert( pin == TEST_GPIO_PIN_FULL );

  assert_fails( rtems_gpio_pin_by_name( fd, "NOT_A_PIN", &pin ), ENOENT );
}

/*
 * The reason this API exists.  A pin the board has spoken for is refused
 * before the driver is asked to do anything, and the refusal is EACCES
 * rather than EBUSY: nothing is going to release it.
 */
static void test_reserved_pin_is_refused( int fd )
{
  rtems_gpio_config   config;
  rtems_gpio_pin_info info;

  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_OUTPUT;

  assert_fails(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_RESERVED, &config ),
    EACCES
  );

  /* And the refusal left it alone, rather than configuring it and failing. */
  rtems_test_assert(
    rtems_gpio_pin_get_info( fd, TEST_GPIO_PIN_RESERVED, &info ) == 0
  );
  rtems_test_assert( ( info.flags & RTEMS_GPIO_PIN_IN_USE ) == 0 );

  /* A pin the package does not bring out is not there to be used either. */
  assert_fails(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_NO_PAD, &config ),
    ENODEV
  );

  /* Strapping is a warning, not a refusal: it configures. */
  rtems_test_assert(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_STRAPPING, &config ) == 0
  );
  rtems_test_assert(
    rtems_gpio_pin_release( fd, TEST_GPIO_PIN_STRAPPING ) == 0
  );
}

/*
 * A capability the pin did not report is ENOTSUP, and it is decided from
 * the pin's information before the driver is called.  So a driver need not
 * check, and more to the point cannot forget to.
 */
static void test_capabilities_are_enforced( int fd )
{
  rtems_gpio_config config;

  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_OUTPUT;
  config.drive = RTEMS_GPIO_DRIVE_OPEN_DRAIN;

  assert_fails(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_VIRTUAL, &config ),
    ENOTSUP
  );

  /* An output on an input-only pin. */
  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_OUTPUT;
  assert_fails(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_INPUT, &config ),
    ENOTSUP
  );

  /* A level trigger on a part with only edges. */
  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_INPUT;
  config.trigger = RTEMS_GPIO_TRIGGER_LEVEL_HIGH;
  assert_fails(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL, &config ),
    ENOTSUP
  );

  /* The same pin, an edge trigger, and it configures. */
  config.trigger = RTEMS_GPIO_TRIGGER_EDGE_RISING;
  rtems_test_assert(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL, &config ) == 0
  );
  rtems_test_assert( rtems_gpio_pin_release( fd, TEST_GPIO_PIN_FULL ) == 0 );
}

static void test_configure_and_report( int fd )
{
  rtems_gpio_config config;
  rtems_gpio_config read_back;

  /* Nothing has configured it, so it reports as unconfigured, not as an
   * error: that is the answer a caller deciding whether a pin is free
   * wants. */
  rtems_test_assert(
    rtems_gpio_pin_get_configuration( fd, TEST_GPIO_PIN_FULL, &read_back ) == 0
  );
  rtems_test_assert( read_back.direction == RTEMS_GPIO_DIRECTION_NONE );

  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_OUTPUT;
  config.bias = RTEMS_GPIO_BIAS_PULL_UP;
  config.initial_value = 1;
  config.debounce = 20000;

  rtems_test_assert(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL, &config ) == 0
  );

  rtems_test_assert(
    rtems_gpio_pin_get_configuration( fd, TEST_GPIO_PIN_FULL, &read_back ) == 0
  );
  rtems_test_assert( read_back.direction == RTEMS_GPIO_DIRECTION_OUTPUT );
  rtems_test_assert( read_back.bias == RTEMS_GPIO_BIAS_PULL_UP );
  rtems_test_assert( read_back.debounce == 20000 );
  rtems_test_assert( read_back.initial_value == 1 );

  /* Configuring it set the level, so the pad is already where it should be
   * rather than glitching low until someone writes it. */
  rtems_test_assert( test_gpio_raw_level( TEST_GPIO_PIN_FULL ) == 1 );

  /* A second claimant is told, rather than quietly winning. */
  assert_fails(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL, &config ),
    EBUSY
  );

  rtems_test_assert( rtems_gpio_pin_release( fd, TEST_GPIO_PIN_FULL ) == 0 );

  /* Released, so it configures again. */
  rtems_test_assert(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL, &config ) == 0
  );
  rtems_test_assert( rtems_gpio_pin_release( fd, TEST_GPIO_PIN_FULL ) == 0 );
}

/*
 * A driver is allowed to round.  What it may not do is round silently, so
 * what comes back from configure() and from get_configuration() is what was
 * applied and not what was asked for.
 */
static void test_rounding_is_reported( int fd )
{
  rtems_gpio_config config;
  rtems_gpio_config read_back;

  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_OUTPUT;
  config.drive_strength = 8000;

  rtems_test_assert(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL, &config ) == 0
  );
  rtems_test_assert( config.drive_strength == TEST_GPIO_DRIVE_STRENGTH );

  rtems_test_assert(
    rtems_gpio_pin_get_configuration( fd, TEST_GPIO_PIN_FULL, &read_back ) == 0
  );
  rtems_test_assert( read_back.drive_strength == TEST_GPIO_DRIVE_STRENGTH );

  rtems_test_assert( rtems_gpio_pin_release( fd, TEST_GPIO_PIN_FULL ) == 0 );
}

static void test_read_write( int fd )
{
  rtems_gpio_config config;
  int               value;

  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_BIDIRECTIONAL;
  rtems_test_assert(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL, &config ) == 0
  );

  rtems_test_assert( rtems_gpio_pin_set( fd, TEST_GPIO_PIN_FULL, 1 ) == 0 );
  rtems_test_assert( test_gpio_raw_level( TEST_GPIO_PIN_FULL ) == 1 );
  rtems_test_assert( rtems_gpio_pin_get( fd, TEST_GPIO_PIN_FULL, &value ) == 0 );
  rtems_test_assert( value == 1 );

  rtems_test_assert( rtems_gpio_pin_toggle( fd, TEST_GPIO_PIN_FULL ) == 0 );
  rtems_test_assert( test_gpio_raw_level( TEST_GPIO_PIN_FULL ) == 0 );
  rtems_test_assert( rtems_gpio_pin_get( fd, TEST_GPIO_PIN_FULL, &value ) == 0 );
  rtems_test_assert( value == 0 );

  rtems_test_assert( rtems_gpio_pin_release( fd, TEST_GPIO_PIN_FULL ) == 0 );

  /* Driving a pin nobody configured is how an off-by-one reaches a pad that
   * belongs to another driver. */
  assert_fails( rtems_gpio_pin_set( fd, TEST_GPIO_PIN_FULL, 1 ), EBADF );
  assert_fails( rtems_gpio_pin_get( fd, TEST_GPIO_PIN_FULL, &value ), EBADF );
}

/*
 * An active-low pin speaks logical levels, and the pad goes the other way.
 * Asserting against the pad rather than against another API call is the
 * only way to tell inversion from a pair of matching mistakes.
 */
static void test_active_low( int fd )
{
  rtems_gpio_config config;
  int               value;

  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_BIDIRECTIONAL;
  config.flags = RTEMS_GPIO_FLAG_ACTIVE_LOW;
  config.initial_value = 1;

  rtems_test_assert(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL2, &config ) == 0
  );

  rtems_test_assert( test_gpio_raw_level( TEST_GPIO_PIN_FULL2 ) == 0 );
  rtems_test_assert(
    rtems_gpio_pin_get( fd, TEST_GPIO_PIN_FULL2, &value ) == 0
  );
  rtems_test_assert( value == 1 );

  rtems_test_assert( rtems_gpio_pin_set( fd, TEST_GPIO_PIN_FULL2, 0 ) == 0 );
  rtems_test_assert( test_gpio_raw_level( TEST_GPIO_PIN_FULL2 ) == 1 );

  rtems_test_assert( rtems_gpio_pin_release( fd, TEST_GPIO_PIN_FULL2 ) == 0 );
}

/*
 * The multiple-pin calls exist so that pins change together.  The part
 * worth testing is the failure: a list with one bad pin in it changes
 * nothing at all, because a half-applied bus is worse than a refused one.
 */
static void test_multiple( int fd )
{
  rtems_gpio_config   config;
  rtems_gpio_pin_list list;

  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_BIDIRECTIONAL;

  rtems_test_assert(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL, &config ) == 0
  );
  rtems_test_assert(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL2, &config ) == 0
  );

  memset( &list, 0, sizeof( list ) );
  list.count = 2;
  list.pins[ 0 ] = TEST_GPIO_PIN_FULL;
  list.pins[ 1 ] = TEST_GPIO_PIN_FULL2;
  list.values = 0x3;

  rtems_test_assert( rtems_gpio_pin_set_multiple( fd, &list ) == 0 );
  rtems_test_assert( test_gpio_raw_level( TEST_GPIO_PIN_FULL ) == 1 );
  rtems_test_assert( test_gpio_raw_level( TEST_GPIO_PIN_FULL2 ) == 1 );

  list.values = 0x1;
  rtems_test_assert( rtems_gpio_pin_set_multiple( fd, &list ) == 0 );
  rtems_test_assert( test_gpio_raw_level( TEST_GPIO_PIN_FULL ) == 1 );
  rtems_test_assert( test_gpio_raw_level( TEST_GPIO_PIN_FULL2 ) == 0 );

  list.values = 0;
  rtems_test_assert( rtems_gpio_pin_get_multiple( fd, &list ) == 0 );
  rtems_test_assert( list.values == 0x1 );

  /* Third pin is not configured, so none of the three is written. */
  list.count = 3;
  list.pins[ 2 ] = TEST_GPIO_PIN_FULL3;
  list.values = 0x0;
  assert_fails( rtems_gpio_pin_set_multiple( fd, &list ), EBADF );
  rtems_test_assert( test_gpio_raw_level( TEST_GPIO_PIN_FULL ) == 1 );

  /* More pins than the list can hold. */
  list.count = RTEMS_GPIO_PIN_LIST_MAX + 1;
  assert_fails( rtems_gpio_pin_set_multiple( fd, &list ), EINVAL );

  rtems_test_assert( rtems_gpio_pin_release( fd, TEST_GPIO_PIN_FULL ) == 0 );
  rtems_test_assert( rtems_gpio_pin_release( fd, TEST_GPIO_PIN_FULL2 ) == 0 );
}

static uint32_t test_irq_pin;
static void    *test_irq_arg;
static int      test_irq_count;

static void test_irq_handler( uint32_t pin, void *arg )
{
  test_irq_pin = pin;
  test_irq_arg = arg;
  ++test_irq_count;
}

static void test_interrupts( int fd )
{
  rtems_gpio_config config;
  int               token = 0;

  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_INPUT;
  config.trigger = RTEMS_GPIO_TRIGGER_EDGE_BOTH;

  rtems_test_assert(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL, &config ) == 0
  );

  /* A handler is required, and a null one is caught here rather than at the
   * first edge. */
  assert_fails(
    rtems_gpio_pin_irq_enable( fd, TEST_GPIO_PIN_FULL, NULL, NULL ),
    EINVAL
  );

  rtems_test_assert(
    rtems_gpio_pin_irq_enable(
      fd,
      TEST_GPIO_PIN_FULL,
      test_irq_handler,
      &token
    ) == 0
  );

  test_irq_count = 0;
  test_gpio_fire_irq( TEST_GPIO_PIN_FULL );
  rtems_test_assert( test_irq_count == 1 );
  rtems_test_assert( test_irq_pin == TEST_GPIO_PIN_FULL );
  rtems_test_assert( test_irq_arg == &token );

  rtems_test_assert(
    rtems_gpio_pin_irq_disable( fd, TEST_GPIO_PIN_FULL ) == 0
  );

  test_gpio_fire_irq( TEST_GPIO_PIN_FULL );
  rtems_test_assert( test_irq_count == 1 );

  rtems_test_assert( rtems_gpio_pin_release( fd, TEST_GPIO_PIN_FULL ) == 0 );
}

/*
 * A driver that implements only pin_get_info() is a legal driver.  Every
 * other operation answers ENOTSUP, and none of them calls through a null
 * pointer to get there.
 */
static void test_unimplemented( void )
{
  rtems_gpio_config   config;
  rtems_gpio_pin_info info;
  rtems_gpio_pin_list list;
  int                 fd;
  int                 value;

  fd = open( GPIO_MIN_PATH, O_RDWR );
  rtems_test_assert( fd >= 0 );

  /* The one handler it has still works. */
  rtems_test_assert(
    rtems_gpio_pin_get_info( fd, TEST_GPIO_PIN_FULL, &info ) == 0
  );

  memset( &config, 0, sizeof( config ) );
  config.direction = RTEMS_GPIO_DIRECTION_OUTPUT;

  assert_fails(
    rtems_gpio_pin_configure( fd, TEST_GPIO_PIN_FULL, &config ),
    ENOTSUP
  );
  assert_fails(
    rtems_gpio_pin_get_configuration( fd, TEST_GPIO_PIN_FULL, &config ),
    ENOTSUP
  );
  assert_fails( rtems_gpio_pin_release( fd, TEST_GPIO_PIN_FULL ), ENOTSUP );
  assert_fails( rtems_gpio_pin_get( fd, TEST_GPIO_PIN_FULL, &value ), ENOTSUP );
  assert_fails( rtems_gpio_pin_set( fd, TEST_GPIO_PIN_FULL, 1 ), ENOTSUP );
  assert_fails( rtems_gpio_pin_toggle( fd, TEST_GPIO_PIN_FULL ), ENOTSUP );
  assert_fails(
    rtems_gpio_pin_irq_disable( fd, TEST_GPIO_PIN_FULL ),
    ENOTSUP
  );

  memset( &list, 0, sizeof( list ) );
  list.count = 1;
  list.pins[ 0 ] = TEST_GPIO_PIN_FULL;
  assert_fails( rtems_gpio_pin_set_multiple( fd, &list ), ENOTSUP );
  assert_fails( rtems_gpio_pin_get_multiple( fd, &list ), ENOTSUP );

  /* Something that is not one of ours. */
  assert_fails( ioctl( fd, _IOR( 'G', 200, int ), &value ), ENOTTY );

  rtems_test_assert( close( fd ) == 0 );
}

/*
 * A controller that cannot answer what a pin is cannot be registered: every
 * check in the generic layer is built on that answer, and registering it
 * anyway would mean skipping them without saying so.
 */
static void test_registration_is_checked( void )
{
  static const rtems_gpio_handlers no_info = { .pin_get_info = NULL };
  rtems_gpio_ctrl                  ctrl;

  memset( &ctrl, 0, sizeof( ctrl ) );
  rtems_test_assert( rtems_gpio_ctrl_init( NULL ) == EINVAL );

  ctrl.handlers = NULL;
  ctrl.pin_count = 4;
  rtems_test_assert( rtems_gpio_ctrl_init( &ctrl ) == EINVAL );

  ctrl.handlers = &no_info;
  rtems_test_assert( rtems_gpio_ctrl_init( &ctrl ) == EINVAL );

  /* A controller with no pins is not a controller. */
  ctrl.handlers = &no_info;
  ctrl.pin_count = 0;
  rtems_test_assert( rtems_gpio_ctrl_init( &ctrl ) == EINVAL );
}

static void run_test( void )
{
  rtems_resource_snapshot snapshot;
  int                     fd;
  int                     warm_up;

  rtems_test_assert( test_gpio_register( GPIO_PATH ) == 0 );
  rtems_test_assert( test_gpio_register_minimal( GPIO_MIN_PATH ) == 0 );

  /*
   * Both nodes are opened and closed once before the baseline is taken.
   * The first open of an IMFS node allocates things it keeps deliberately,
   * and counting those as a leak reports a failure that is not one.
   */
  warm_up = open( GPIO_PATH, O_RDWR );
  rtems_test_assert( warm_up >= 0 );
  rtems_test_assert( close( warm_up ) == 0 );
  warm_up = open( GPIO_MIN_PATH, O_RDWR );
  rtems_test_assert( warm_up >= 0 );
  rtems_test_assert( close( warm_up ) == 0 );

  rtems_resource_snapshot_take( &snapshot );

  fd = open( GPIO_PATH, O_RDWR );
  rtems_test_assert( fd >= 0 );

  test_controller_info( fd );
  test_pin_info( fd );
  test_pin_by_name( fd );
  test_reserved_pin_is_refused( fd );
  test_capabilities_are_enforced( fd );
  test_configure_and_report( fd );
  test_rounding_is_reported( fd );
  test_read_write( fd );
  test_active_low( fd );
  test_multiple( fd );
  test_interrupts( fd );
  test_unimplemented();
  test_registration_is_checked();

  rtems_test_assert( close( fd ) == 0 );

  /*
   * Covers rather more than the heap: a descriptor left open by
   * test_unimplemented(), or a mutex left behind by a controller that
   * failed to register, shows up here and in no other assertion in this
   * file.
   */
  rtems_test_assert( rtems_resource_snapshot_check( &snapshot ) );
}

static rtems_task Init( rtems_task_argument arg )
{
  (void) arg;

  TEST_BEGIN();

  run_test();

  TEST_END();
  rtems_test_exit( 0 );
}

#define CONFIGURE_APPLICATION_DOES_NOT_NEED_CLOCK_DRIVER

#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 8

#define CONFIGURE_MAXIMUM_TASKS 1

#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT

#include <rtems/confdefs.h>
