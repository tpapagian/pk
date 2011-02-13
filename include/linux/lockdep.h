/*
 * Runtime locking correctness validator
 *
 *  Copyright (C) 2006,2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 * see Documentation/lockdep-design.txt for more details.
 */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

#include <linux/lock_debug_hooks.h>

struct task_struct;
struct lockdep_map;

/* for sysctl */
extern int prove_locking;

#if defined(CONFIG_LOCKDEP) || defined(CONFIG_LOCK_STAT2)
#include <linux/lockstat.h>
#endif

#ifdef CONFIG_LOCKDEP

#include <linux/linkage.h>
#include <linux/list.h>
#include <linux/debug_locks.h>
#include <linux/stacktrace.h>

extern struct lock_class_key __lockdep_no_validate__;

/*
 * Every lock has a list of other locks that were taken after it.
 * We only grow the list, never remove from it:
 */
struct lock_list {
	struct list_head		entry;
	struct lock_class		*class;
	struct stack_trace		trace;
	int				distance;

	/*
	 * The parent field is used to implement breadth-first search, and the
	 * bit 0 is reused to indicate if the lock has been accessed in BFS.
	 */
	struct lock_list		*parent;
};

/*
 * We record lock dependency chains, so that we can cache them:
 */
struct lock_chain {
	u8				irq_context;
	u8				depth;
	u16				base;
	struct list_head		entry;
	u64				chain_key;
};

/*
 * Initialization, self-test and debugging-output methods:
 */
extern void lockdep_info(void);
extern void lockdep_reset(void);
extern void lockdep_reset_lock(struct lockdep_map *lock);
extern void lockdep_free_key_range(void *start, unsigned long size);
extern void lockdep_sys_exit(void);

extern void lockdep_off(void);
extern void lockdep_on(void);

/*
 * These methods are used by specific locking variants (spinlocks,
 * rwlocks, mutexes and rwsems) to pass init/acquire/release events
 * to lockdep:
 */


/*
 * To initialize a lockdep_map statically use this macro.
 * Note that _name must not be NULL.
 */
#define STATIC_LOCKDEP_MAP_INIT(_name, _key) \
	{ .name = (_name), .key = (void *)(_key), }

/*
 * Reinitialize a lock key - for cases where there is special locking or
 * special initialization of locks so that the validator gets the scope
 * of dependencies wrong: they are either too broad (they need a class-split)
 * or they are too narrow (they suffer from a false class-split):
 */
#define lockdep_set_class(lock, key) \
		lockdep_init_map(&(lock)->dep_map, #key, key, 0)
#define lockdep_set_class_and_name(lock, key, name) \
		lockdep_init_map(&(lock)->dep_map, name, key, 0)
#define lockdep_set_class_and_subclass(lock, key, sub) \
		lockdep_init_map(&(lock)->dep_map, #key, key, sub)
#define lockdep_set_subclass(lock, sub)	\
		lockdep_init_map(&(lock)->dep_map, #lock, \
				 (lock)->dep_map.key, sub)

#define lockdep_set_novalidate_class(lock) \
	lockdep_set_class(lock, &__lockdep_no_validate__)
/*
 * Compare locking classes
 */
#define lockdep_match_class(lock, key) lockdep_match_key(&(lock)->dep_map, key)

static inline int lockdep_match_key(struct lockdep_map *lock,
				    struct lock_class_key *key)
{
	return lock->key == key;
}

#define lockdep_is_held(lock)	lock_is_held(&(lock)->dep_map)

extern int lock_is_held(struct lockdep_map *lock);

extern void lock_set_class(struct lockdep_map *lock, const char *name,
			   struct lock_class_key *key, unsigned int subclass,
			   unsigned long ip);

static inline void lock_set_subclass(struct lockdep_map *lock,
		unsigned int subclass, unsigned long ip)
{
	lock_set_class(lock, lock->name, lock->key, subclass, ip);
}

extern void lockdep_set_current_reclaim_state(gfp_t gfp_mask);
extern void lockdep_clear_current_reclaim_state(void);
extern void lockdep_trace_alloc(gfp_t mask);

# define INIT_LOCKDEP				.lockdep_recursion = 0, .lockdep_reclaim_gfp = 0,

#define lockdep_depth(tsk)	(debug_locks ? (tsk)->lockdep_depth : 0)

#define lockdep_assert_held(l)	WARN_ON(debug_locks && !lockdep_is_held(l))

#else /* !LOCKDEP */

static inline void lockdep_off(void)
{
}

static inline void lockdep_on(void)
{
}

# define lock_set_class(l, n, k, s, i)		do { } while (0)
# define lock_set_subclass(l, s, i)		do { } while (0)
# define lockdep_set_current_reclaim_state(g)	do { } while (0)
# define lockdep_clear_current_reclaim_state()	do { } while (0)
# define lockdep_trace_alloc(g)			do { } while (0)
# define lockdep_info()				do { } while (0)
# define lockdep_set_class(lock, key)		do { (void)(key); } while (0)
# define lockdep_set_class_and_name(lock, key, name) \
		do { (void)(key); (void)(name); } while (0)
#define lockdep_set_class_and_subclass(lock, key, sub) \
		do { (void)(key); } while (0)
#define lockdep_set_subclass(lock, sub)		do { } while (0)

#define lockdep_set_novalidate_class(lock) do { } while (0)

/*
 * We don't define lockdep_match_class() and lockdep_match_key() for !LOCKDEP
 * case since the result is not well defined and the caller should rather
 * #ifdef the call himself.
 */

# define INIT_LOCKDEP
# define lockdep_reset()		do { debug_locks = 1; } while (0)
# define lockdep_free_key_range(start, size)	do { } while (0)
# define lockdep_sys_exit() 			do { } while (0)

#define lockdep_depth(tsk)	(0)

#define lockdep_assert_held(l)			do { } while (0)

#endif /* !LOCKDEP */

#ifdef CONFIG_LOCKDEP
extern void lockdep_check_flags(unsigned long flags);
#else
static inline void lockdep_check_flags(unsigned long flags)
{
}
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
extern void early_boot_irqs_off(void);
extern void early_boot_irqs_on(void);
extern void print_irqtrace_events(struct task_struct *curr);
#else
static inline void early_boot_irqs_off(void)
{
}
static inline void early_boot_irqs_on(void)
{
}
static inline void print_irqtrace_events(struct task_struct *curr)
{
}
#endif

/*
 * For trivial one-depth nesting of a lock-class, the following
 * global define can be used. (Subsystems with multiple levels
 * of nesting should define their own lock-nesting subclasses.)
 */
#define SINGLE_DEPTH_NESTING			1

#ifdef CONFIG_PROVE_LOCKING
# define might_lock(lock) 						\
do {									\
	typecheck(struct lockdep_map *, &(lock)->dep_map);		\
	lock_acquire(&(lock)->dep_map, 0, 0, 0, 2, NULL, _THIS_IP_);	\
	lock_release(&(lock)->dep_map, 0, _THIS_IP_);			\
} while (0)
# define might_lock_read(lock) 						\
do {									\
	typecheck(struct lockdep_map *, &(lock)->dep_map);		\
	lock_acquire(&(lock)->dep_map, 0, 0, 1, 2, NULL, _THIS_IP_);	\
	lock_release(&(lock)->dep_map, 0, _THIS_IP_);			\
} while (0)
#else
# define might_lock(lock) do { } while (0)
# define might_lock_read(lock) do { } while (0)
#endif

#ifdef CONFIG_PROVE_RCU
extern void lockdep_rcu_dereference(const char *file, const int line);
#endif


#ifdef CONFIG_LOCK_DEBUG_HOOKS
extern void lockdep_init(void);
#else
# define lockdep_init()				do { } while (0)
#endif

#ifdef CONFIG_LOCK_DEBUG_HOOKS
extern void lockdep_init_map(struct lockdep_map *lock, const char *name,
			     struct lock_class_key *key, int subclass);
#else
# define lockdep_init_map(lock, name, key, sub) \
		do { (void)(name); (void)(key); } while (0)
#endif

#endif /* __LINUX_LOCKDEP_H */
