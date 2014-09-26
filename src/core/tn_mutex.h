/*******************************************************************************
 *
 * TNeoKernel: real-time kernel initially based on TNKernel
 *
 *    TNKernel:                  copyright © 2004, 2013 Yuri Tiomkin.
 *    PIC32-specific routines:   copyright © 2013, 2014 Anders Montonen.
 *    TNeoKernel:                copyright © 2014       Dmitry Frank.
 *
 *    TNeoKernel was born as a thorough review and re-implementation of
 *    TNKernel. The new kernel has well-formed code, inherited bugs are fixed
 *    as well as new features being added, and it is tested carefully with
 *    unit-tests.
 *
 *    API is changed somewhat, so it's not 100% compatible with TNKernel,
 *    hence the new name: TNeoKernel.
 *
 *    Permission to use, copy, modify, and distribute this software in source
 *    and binary forms and its documentation for any purpose and without fee
 *    is hereby granted, provided that the above copyright notice appear
 *    in all copies and that both that copyright notice and this permission
 *    notice appear in supporting documentation.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE DMITRY FRANK AND CONTRIBUTORS "AS IS"
 *    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *    PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL DMITRY FRANK OR CONTRIBUTORS BE
 *    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *    THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

/**
 * \file
 *
 * A mutex is an object used to protect shared resource.
 *
 * While mutex is seemingly similar to a semaphore with maximum count of `1`
 * (binary semaphore), their usage is very different: as mentioned above, the
 * purpose of mutex is to protect shared resource. A locked mutex is "owned" by
 * the task that locked it, and only the same task may unlock it. This
 * ownership allows to implement algorithms to prevent priority inversion.
 * So, mutex is a *locking mechanism*.
 *
 * Binary semaphore, on the other hand, is *signaling mechanism*. It's quite
 * legal and encouraged for semaphore to be acquired in the task A, and then
 * released in task B.
 *
 * You might want to take a look at this post:
 * http://stackoverflow.com/a/86021/1099240
 *
 * ---------------------------------------------------------------------------
 *
 * Mutex supports two approaches for avoiding the unbounded priority inversions
 * problem - the priority inheritance protocol and the priority ceiling
 * protocol. A discussion about strengths and weaknesses of each protocol as
 * well as priority inversions problem is beyond the scope of this document.
 *
 * A mutex uses the priority inheritance protocol when it has been created with
 * the `TN_MUTEX_PROT_INHERIT` attribute, and the priority ceiling protocol
 * when its attribute value is `TN_MUTEX_PROT_CEILING`.
 *
 * The priority inheritance protocol solves the priority inversions problem but
 * doesn't prevents deadlocks, although the kernel can notify you if a deadlock
 * has occured (see `TN_MUTEX_DEADLOCK_DETECT`).
 *
 * The priority ceiling protocol prevents deadlocks and chained blocking but it
 * is slower than the priority inheritance protocol.
 *
 * @see `TN_MUTEX_DEADLOCK_DETECT`
 */

#ifndef _TN_MUTEX_H
#define _TN_MUTEX_H

/*******************************************************************************
 *    INCLUDED FILES
 ******************************************************************************/

#include "tn_list.h"
#include "tn_common.h"


/*******************************************************************************
 *    PUBLIC TYPES
 ******************************************************************************/

/**
 * Mutex protocol for avoid priority inversion
 */
enum TN_MutexProtocol {
   ///
   /// Mutex uses priority ceiling protocol
   TN_MUTEX_PROT_CEILING = 1,
   ///
   /// Mutex uses priority inheritance protocol
   TN_MUTEX_PROT_INHERIT = 2,
};


/**
 * Mutex
 */
struct TN_Mutex {
   ///
   /// List of tasks that wait a mutex
   struct TN_ListItem wait_queue;
   ///
   /// To include in task's locked mutexes list (if any)
   struct TN_ListItem mutex_queue;
#if TN_MUTEX_DEADLOCK_DETECT
   ///
   /// List of other mutexes involved in deadlock
   /// (normally, this list is empty)
   struct TN_ListItem deadlock_list;
#endif
   ///
   /// Mutex protocol: priority ceiling or priority inheritance
   enum TN_MutexProtocol protocol;
   ///
   /// Current mutex owner (task that locked mutex)
   struct TN_Task *holder;
   ///
   /// Used if only protocol is `TN_MUTEX_PROT_CEILING`:
   /// maximum priority of task that may lock the mutex
   int ceil_priority;
   ///
   /// Lock count (for recursive locking)
   int cnt;
   ///
   /// id for object validity verification
   enum TN_ObjId id_mutex;
};

/*******************************************************************************
 *    GLOBAL VARIABLES
 ******************************************************************************/

/*******************************************************************************
 *    DEFINITIONS
 ******************************************************************************/

#define get_mutex_by_mutex_queque(que)              \
   (que ? container_of(que, struct TN_Mutex, mutex_queue) : 0)

#define get_mutex_by_wait_queque(que)               \
   (que ? container_of(que, struct TN_Mutex, wait_queue) : 0)

#define get_mutex_by_lock_mutex_queque(que) \
   (que ? container_of(que, struct TN_Mutex, mutex_queue) : 0)




/*******************************************************************************
 *    PUBLIC FUNCTION PROTOTYPES
 ******************************************************************************/

/**
 * Construct mutex. The field `id_mutex` should not contain `TN_ID_MUTEX`, 
 * otherwise, `TN_RC_WPARAM` is returned.
 *
 * @param mutex
 *    Pointer to already allocated `struct TN_Mutex`
 * @param protocol
 *    Mutex protocol: priority ceiling or priority inheritance.
 *    See `enum TN_MutexProtocol`.
 * @param ceil_priority
 *    Used if only `protocol` is `TN_MUTEX_PROT_CEILING`: maximum priority
 *    of the task that may lock the mutex.
 *
 * @return  `TN_RC_OK`
 */
enum TN_RCode tn_mutex_create(
      struct TN_Mutex        *mutex,
      enum TN_MutexProtocol   protocol,
      int                     ceil_priority
      );

/**
 * Destruct mutex.
 *
 * All tasks that wait for lock the mutex become runnable with
 * `TN_RC_DELETED` code returned.
 *
 * @param mutex      mutex to destruct
 */
enum TN_RCode tn_mutex_delete(struct TN_Mutex *mutex);

/**
 * Lock mutex.
 *
 *    * If the mutex is not locked, function immediately locks the mutex and 
 *      returns `TN_RC_OK`.
 *    * If the mutex is already locked by the same task, lock count is merely
 *      incremented and `TN_RC_OK` is returned immediately.
 *    * If the mutex is locked by different task, behavior depends on
 *      `timeout` value: refer to `TN_Timeout`.
 *
 * @param mutex      mutex to lock
 * @param timeout    refer to `TN_Timeout`
 *
 * @return
 *    * `TN_RC_OK` if mutex is successfully locked or if lock count was
 *      merely incremented (this is possible if recursive locking is enabled,
 *      see `TN_MUTEX_REC`)
 *    * `TN_RC_ILLEGAL_USE` 
 *       * if mutex protocol is `TN_MUTEX_PROT_CEILING`
 *         and calling task's priority is higher than `ceil_priority`
 *         given to `tn_mutex_create()`
 *       * if recursive locking is disabled (see `TN_MUTEX_REC`)
 *         and the mutex is already locked by calling task
 *    * Other possible return codes depend on `timeout` value,
 *      refer to `TN_Timeout`
 *
 * @see `TN_Timeout`
 * @see `TN_MutexProtocol`
 * @see `TN_MUTEX_REC`
 */
enum TN_RCode tn_mutex_lock(struct TN_Mutex *mutex, TN_Timeout timeout);

/**
 * The same as `tn_mutex_lock()` with zero timeout
 */
enum TN_RCode tn_mutex_lock_polling(struct TN_Mutex *mutex);

/**
 * Unlock mutex.
 *
 *    * If mutex is not locked or locked by different task, `TN_RC_ILLEGAL_USE`
 *      is returned.
 *    * If mutex is already locked by calling task, lock count is decremented.
 *      Now, if lock count is zero, mutex gets unlocked (and if there are
 *      task(s) waiting for mutex, the first one from the wait queue locks the
 *      mutex).  Otherwise, mutex remains locked with lock count decremented
 *      and function returns `TN_RC_OK`.
 *
 * @return
 *    * `TN_RC_OK` if mutex is unlocked of if lock count was merely decremented
 *      (this is possible if recursive locking is enabled, see `TN_MUTEX_REC`)
 *    * `TN_RC_ILLEGAL_USE` if mutex is either not locked or locked by
 *      different task
 *
 */
enum TN_RCode tn_mutex_unlock(struct TN_Mutex *mutex);


#endif // _TN_MUTEX_H

/*******************************************************************************
 *    end of file
 ******************************************************************************/

