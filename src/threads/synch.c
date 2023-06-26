/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */

// utils start
bool if_lock_acquired(struct lock *l)
{
  return l->holder != NULL;
}
bool is_lock_null(struct lock *l)
{
  return l == NULL;
}
void set_thread_wait_lock(struct thread *t, struct lock *l)
{
  t->ext_props.lock_waiting = l;
}
void update_lock_priority(struct lock *l, int priority)
{
  l->priority = priority;
}
void set_lock_holder(struct lock *l, struct thread *t)
{
  l->holder = t;
}

// utils end

void sema_init(struct semaphore *sema, unsigned value)
{
  ASSERT(sema != NULL);

  sema->value = value;
  list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */

bool thread_comparator(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct thread *t1 = list_entry(a, struct thread, elem);
  struct thread *t2 = list_entry(b, struct thread, elem);
  return t1->priority > t2->priority;
}

void sema_down(struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT(sema != NULL);
  ASSERT(!intr_context());

  old_level = intr_disable();
  while (sema->value == 0)
  {
    list_insert_ordered(&sema->waiters, &thread_current()->elem, thread_comparator, NULL);
    thread_block();
  }
  sema->value--;
  intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
  enum intr_level old_level;
  bool success;

  ASSERT(sema != NULL);

  old_level = intr_disable();
  if (sema->value > 0)
  {
    sema->value--;
    success = true;
  }
  else
    success = false;
  intr_set_level(old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */

void sema_up(struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT(sema != NULL);

  old_level = intr_disable();
  if (!list_empty(&sema->waiters))
  {
    list_sort(&sema->waiters, thread_comparator, NULL);
    struct thread *top_thread = list_entry(list_pop_front(&sema->waiters), struct thread, elem);
    thread_unblock(top_thread);
  }

  sema->value++;
  thread_yield();
  intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
  struct semaphore sema[2];
  int i;

  printf("Testing semaphores...");
  sema_init(&sema[0], 0);
  sema_init(&sema[1], 0);
  thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++)
  {
    sema_up(&sema[0]);
    sema_down(&sema[1]);
  }
  printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++)
  {
    sema_down(&sema[0]);
    sema_up(&sema[1]);
  }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock)
{
  ASSERT(lock != NULL);

  set_lock_holder(lock, NULL);
  sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

/*
  comparator used in list_insert_ordered for locks acquired for each thread based on the lock priority value
  - here lock priority value is the max priority of thread waiting to acquire this lock
*/
bool lock_comparator(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  return list_entry(a, struct lock, elem)->priority > list_entry(b, struct lock, elem)->priority;
}

void add_lock_to_acquired_list(struct lock *lock)
{
  enum intr_level old_level = intr_disable();
  struct thread *current_thread = thread_current();
  list_insert_ordered(&current_thread->ext_props.locks_acquired, &lock->elem, lock_comparator, NULL);

  if (lock->priority > current_thread->priority)
  {
    current_thread->priority = lock->priority;
    thread_yield();
  }

  intr_set_level(old_level);
}

bool comp_priority(const struct list_elem *elem1, const struct list_elem *elem2)
{
  // printf("entering\n");
  struct lock *l1 = list_entry(elem1, struct lock, elem);
  struct lock *l2 = list_entry(elem2, struct lock, elem);
  // printf("%d , %d\n", l1->priority, l2->priority);
  return l1->priority > l2->priority;
}

void thread_donate(struct lock *l, struct thread *t)
{
  if (!is_lock_null(l) && (l->priority <= t->priority))
  {
    update_lock_priority(l, t->priority);
    update_priority_by_locks_acquired(l->holder);
    thread_donate(l->holder->ext_props.lock_waiting, t);
  }
}

void lock_acquire(struct lock *lock)
{
  struct thread *current_thread = thread_current();

  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(!lock_held_by_current_thread(lock));

  /*
     - set_thread_wait_lock : add the lock to the current thread lock waiting pointer
     - thread_donate :
      - if the priority of the current thread is greater than the thread to which the lock is held
        update the priority of the thread that got the current lock with the priority of current thread and
        update the priority of the lock with the current thread priority
      - This method also handles the case where there are multiple levels of threads waiting for locks held by
        threads with lower priority compared to current thread
    - if_lock_acquired : check if the lock is held by any thread
  */
  if (if_lock_acquired(lock) && !thread_mlfqs)
  {
    set_thread_wait_lock(current_thread, lock);
    thread_donate(lock, current_thread);
  }
  /*
    once the priority donation is done by the thread (only if the priority of current thread is highrer)
    current thread will wait for the lock in semaphore wait list and waits for its turn
  */
  sema_down(&lock->semaphore);

  enum intr_level old_level = intr_disable();

  current_thread = thread_current();
  if (!thread_mlfqs)
  {
    /*
      Now, current thread got the chance to acquire the lock as it passed sema_down
       - update the current thread waiting for lock to NULL
       - Now that the lock is being picked by the current thread update the priority of the lock accordingly
       - add the current lock to the locks acquired list of the current thread
    */
    set_thread_wait_lock(current_thread, NULL);
    update_lock_priority(lock, current_thread->priority);
    add_lock_to_acquired_list(lock);
  }
  /*
    - set at lock level the holder to current thread
  */
  set_lock_holder(lock, current_thread);

  intr_set_level(old_level);
}
/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
  bool success;

  ASSERT(lock != NULL);
  ASSERT(!lock_held_by_current_thread(lock));

  success = sema_try_down(&lock->semaphore);
  if (success)
    lock->holder = thread_current();
  return success;
}

void release_lock_update_priority(struct lock *lock)
{
  enum intr_level old_level = intr_disable();
  /*
    remove the element from the list once the thread is released by current thread
  */
  list_remove(&lock->elem);
  calculate_priority_by_locks_acquired(thread_current());
  intr_set_level(old_level);
}

void lock_release(struct lock *lock)
{
  ASSERT(lock != NULL);
  ASSERT(lock_held_by_current_thread(lock));
  if (!thread_mlfqs)
    release_lock_update_priority(lock);
  set_lock_holder(lock, NULL);
  sema_up(&lock->semaphore);
}

bool lock_held_by_current_thread(const struct lock *lock)
{
  ASSERT(lock != NULL);

  return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
  struct list_elem elem;      /* List element. */
  struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
  ASSERT(cond != NULL);

  list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT(cond != NULL);
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(lock_held_by_current_thread(lock));

  sema_init(&waiter.semaphore, 0);
  list_push_back(&cond->waiters, &waiter.elem);
  lock_release(lock);
  sema_down(&waiter.semaphore);
  lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* cond sema comparation function */
bool cond_comparator(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
  struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);
  struct list_elem *waiters_top_a = list_front(&sema_a->semaphore.waiters);
  struct list_elem *waiters_top_b = list_front(&sema_b->semaphore.waiters);

  return list_entry(waiters_top_a, struct thread, elem)->priority > list_entry(waiters_top_b, struct thread, elem)->priority;
}
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT(cond != NULL);
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(lock_held_by_current_thread(lock));

  if (!list_empty(&cond->waiters))
  {
    list_sort(&cond->waiters, cond_comparator, NULL);
    struct list_elem *l_elem = list_pop_front(&cond->waiters);
    struct semaphore_elem *sema_elem = list_entry(l_elem, struct semaphore_elem, elem);
    sema_up(&sema_elem->semaphore);
  }
}
/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
  ASSERT(cond != NULL);
  ASSERT(lock != NULL);

  while (!list_empty(&cond->waiters))
    cond_signal(cond, lock);
}
