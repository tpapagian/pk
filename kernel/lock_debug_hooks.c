#include <linux/sched.h>
#include <linux/module.h>
#include <linux/lock_debug_hooks.h>
#include <linux/lockdep.h>
#include <linux/lockstat2.h>

#include "lockstat2_internals.h"
#include "lockdep_internals.h"

#define CREATE_TRACE_POINTS
#include <trace/events/lock.h>

static int lockdep_initialized;

/*
 * All data structures here are protected by the global debug_lock.
 *
 * Mutex key structs only get allocated, once during bootup, and never
 * get freed - this significantly simplifies the debugging code.
 */
unsigned long nr_lock_classes;
struct lock_class lock_classes[MAX_LOCKDEP_KEYS];

extern struct list_head classhash_table[CLASSHASH_SIZE];

/*
 * We are not always called with irqs disabled - do that here,
 * and also avoid lockdep recursion:
 */
void lock_acquire(struct lockdep_map *lock, unsigned int subclass,
			  int trylock, int read, int check,
			  struct lockdep_map *nest_lock, unsigned long ip)
{
	unsigned long flags;

	if (unlikely(current->lockdep_recursion))
		return;

	raw_local_irq_save(flags);
	lockdep_check_flags(flags);

	current->lockdep_recursion = 1;
	trace_lock_acquire(lock, subclass, trylock, read, check, nest_lock, ip);
	lockstat_lock_acquire(lock, subclass, trylock, read,
		       irqs_disabled_flags(flags), nest_lock, ip, 0);
	current->lockdep_recursion = 0;
	raw_local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(lock_acquire);

void lock_release(struct lockdep_map *lock, int nested,
			  unsigned long ip)
{
	unsigned long flags;

	if (unlikely(current->lockdep_recursion))
		return;

	raw_local_irq_save(flags);
	lockdep_check_flags(flags);
	current->lockdep_recursion = 1;
	trace_lock_release(lock, ip);
	lockstat_lock_release(lock, nested, ip);
	current->lockdep_recursion = 0;
	raw_local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(lock_release);

void lock_contended(struct lockdep_map *lock, unsigned long ip)
{
	unsigned long flags;

	if (unlikely(!lock_stat))
		return;

	if (unlikely(current->lockdep_recursion))
		return;

	raw_local_irq_save(flags);
	lockdep_check_flags(flags);
	current->lockdep_recursion = 1;
	trace_lock_contended(lock, ip);
	lockstat_lock_contended(lock, ip);
	current->lockdep_recursion = 0;
	raw_local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(lock_contended);

void lock_acquired(struct lockdep_map *lock, unsigned long ip)
{
	unsigned long flags;

	if (unlikely(!lock_stat))
		return;

	if (unlikely(current->lockdep_recursion))
		return;

	raw_local_irq_save(flags);
	lockdep_check_flags(flags);
	current->lockdep_recursion = 1;
	trace_lock_acquired(lock, ip);
	lockstat_lock_acquired(lock, ip);
	current->lockdep_recursion = 0;
	raw_local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(lock_acquired);

/*
 * Initialize a lock instance's lock-class mapping info:
 */
void lockdep_init_map(struct lockdep_map *lock, const char *name,
		      struct lock_class_key *key, int subclass)
{
	lock->class_cache = NULL;
#ifdef CONFIG_LOCK_STAT2
	lock->cpu = raw_smp_processor_id();
#endif

	if (DEBUG_LOCKS_WARN_ON(!name)) {
		lock->name = "NULL";
		return;
	}

	lock->name = name;

	if (DEBUG_LOCKS_WARN_ON(!key))
		return;
	/*
	 * Sanity check, the lock-class key must be persistent:
	 */
	if (!static_obj(key)) {
		printk("BUG: key %p not in .data!\n", key);
		dump_stack();
		DEBUG_LOCKS_WARN_ON(1);
		return;
	}
	lock->key = key;

	if (unlikely(!debug_locks))
		return;

	if (subclass)
		register_lock_class(lock, subclass, 1);
}
EXPORT_SYMBOL_GPL(lockdep_init_map);

void lockdep_init(void)
{
	int i;

	/*
	 * Some architectures have their own start_kernel()
	 * code which calls lockdep_init(), while we also
	 * call lockdep_init() from the start_kernel() itself,
	 * and we want to initialize the hashes only once:
	 */
	if (lockdep_initialized)
		return;

	for (i = 0; i < CLASSHASH_SIZE; i++)
		INIT_LIST_HEAD(classhash_table + i);

	lockdep_initialized = 1;
}
