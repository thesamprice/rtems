/**
 *  @file
 *
 *  @brief Initialize a Core Mutex
 *  @ingroup ScoreMutex
 */

/*
 *  COPYRIGHT (c) 1989-1999.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems/system.h>
#include <rtems/score/isr.h>
#include <rtems/score/coremuteximpl.h>
#include <rtems/score/thread.h>

Status_Control _CORE_mutex_Initialize(
  CORE_mutex_Control           *the_mutex,
  Thread_Control               *executing,
  const CORE_mutex_Attributes  *the_mutex_attributes,
  bool                          initially_locked
)
{

/* Add this to the RTEMS environment later ?????????
  rtems_assert( initial_lock == CORE_MUTEX_LOCKED ||
                initial_lock == CORE_MUTEX_UNLOCKED );
 */

  the_mutex->Attributes    = *the_mutex_attributes;

  if ( initially_locked ) {
    bool              is_priority_ceiling;
    Priority_Control  ceiling;
    Per_CPU_Control  *cpu_self;

    the_mutex->nest_count = 1;
    the_mutex->holder     = executing;

    /* The mutex initialization is only protected by the allocator lock */
    cpu_self = _Thread_Dispatch_disable();

    is_priority_ceiling =
      _CORE_mutex_Is_priority_ceiling( &the_mutex->Attributes );
    ceiling = the_mutex->Attributes.priority_ceiling;

    /*
     * The test to check for a ceiling violation is a bit arbitrary.  In case
     * this thread is the owner of a priority inheritance mutex, then it may
     * get a higher priority later or anytime on SMP configurations.
     */
    if ( is_priority_ceiling && executing->current_priority < ceiling ) {
      /*
       * There is no need to undo the previous work since this error aborts
       * the object creation.
       */
      _Thread_Dispatch_enable( cpu_self );
      return STATUS_MUTEX_CEILING_VIOLATED;
    }

    executing->resource_count++;

    if ( is_priority_ceiling ) {
      _Thread_Raise_priority( executing, ceiling );
    }

    _Thread_Dispatch_enable( cpu_self );
  } else {
    the_mutex->nest_count = 0;
    the_mutex->holder     = NULL;
  }

  _Thread_queue_Initialize( &the_mutex->Wait_queue );

  return STATUS_SUCCESSFUL;
}
