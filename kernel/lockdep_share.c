#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/stacktrace.h>
#include <linux/debug_locks.h>
#include <linux/irqflags.h>
#include <linux/utsname.h>
#include <linux/hash.h>
#include <linux/ftrace.h>
#include <linux/stringify.h>
#include <linux/bitops.h>
#include <linux/gfp.h>

#include <asm/sections.h>

#include "lockdep_internals.h"
#include "lockstat_internals.h"

#ifdef CONFIG_DEBUG_LOCK_HOOKS
atomic_t ls_non_nested;
atomic_t ls_nested;
#endif

/*
 * lockdep_lock: protects the lockdep graph, the hashes and the
 *               class/list/hash allocators.
 *
 * This is one of the rare exceptions where it's justified
 * to use a raw spinlock - we really dont want the spinlock
 * code to recurse back into the lockdep code...
 */
static arch_spinlock_t lockdep_lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;

static int graph_lock(void)
{
	arch_spin_lock(&lockdep_lock);
	/*
	 * Make sure that if another CPU detected a bug while
	 * walking the graph we dont change it (while the other
	 * CPU is busy printing out stuff with the graph lock
	 * dropped already)
	 */
	if (!debug_locks) {
		arch_spin_unlock(&lockdep_lock);
		return 0;
	}
	/* prevent any recursions within lockdep from causing deadlocks */
	current->lockdep_recursion++;
	return 1;
}

static inline int graph_unlock(void)
{
	if (debug_locks && !arch_spin_is_locked(&lockdep_lock))
		return DEBUG_LOCKS_WARN_ON(1);

	current->lockdep_recursion--;
	arch_spin_unlock(&lockdep_lock);
	return 0;
}

/*
 * Turn lock debugging off and return with 0 if it was off already,
 * and also release the graph lock:
 */
static inline int debug_locks_off_graph_unlock(void)
{
	int ret = debug_locks_off();

	arch_spin_unlock(&lockdep_lock);

	return ret;
}

/*
 * Is this the address of a static object:
 */
int static_obj(void *obj)
{
	unsigned long start = (unsigned long) &_stext,
		      end   = (unsigned long) &_end,
		      addr  = (unsigned long) obj;

	/*
	 * static variable?
	 */
	if ((addr >= start) && (addr < end))
		return 1;

	if (arch_is_kernel_data(addr))
		return 1;

	/*
	 * in-kernel percpu var?
	 */
	if (is_kernel_percpu_address(addr))
		return 1;

	/*
	 * module static or percpu var?
	 */
	return is_module_address(addr) || is_module_percpu_address(addr);
}

/*
 * To make lock name printouts unique, we calculate a unique
 * class->name_version generation counter:
 */
static int count_matching_names(struct lock_class *new_class)
{
	struct lock_class *class;
	int count = 0;

	if (!new_class->name)
		return 0;

	list_for_each_entry(class, &all_lock_classes, lock_entry) {
		if (new_class->key - new_class->subclass == class->key)
			return class->name_version;
		if (class->name && !strcmp(class->name, new_class->name))
			count = max(count, class->name_version);
	}

	return count + 1;
}

/*
 * Register a lock's class in the hash-table, if the class is not present
 * yet. Otherwise we look it up. We cache the result in the lock object
 * itself, so actual lookup of the hash should be once per lock object.
 */
inline struct lock_class *
look_up_lock_class(struct lockdep_map *lock, unsigned int subclass)
{
	struct lockdep_subclass_key *key;
	struct list_head *hash_head;
	struct lock_class *class;

#ifdef CONFIG_DEBUG_LOCKDEP
	/*
	 * If the architecture calls into lockdep before initializing
	 * the hashes then we'll warn about it later. (we cannot printk
	 * right now)
	 */
	if (unlikely(!lockdep_initialized)) {
		lockdep_init();
		lockdep_init_error = 1;
		save_stack_trace(&lockdep_init_trace);
	}
#endif

	/*
	 * Static locks do not have their class-keys yet - for them the key
	 * is the lock object itself:
	 */
	if (unlikely(!lock->key))
		lock->key = (void *)lock;

	/*
	 * NOTE: the class-key must be unique. For dynamic locks, a static
	 * lock_class_key variable is passed in through the mutex_init()
	 * (or spin_lock_init()) call - which acts as the key. For static
	 * locks we use the lock object itself as the key.
	 */
	BUILD_BUG_ON(sizeof(struct lock_class_key) >
			sizeof(struct lockdep_map));

	key = lock->key->subkeys + subclass;

	hash_head = classhashentry(key);

	/*
	 * We can walk the hash lockfree, because the hash only
	 * grows, and we are careful when adding entries to the end:
	 */
	list_for_each_entry(class, hash_head, hash_entry) {
		if (class->key == key) {
			WARN_ON_ONCE(class->name != lock->name);
			return class;
		}
	}

	return NULL;
}

/*
 * Register a lock's class in the hash-table, if the class is not present
 * yet. Otherwise we look it up. We cache the result in the lock object
 * itself, so actual lookup of the hash should be once per lock object.
 */
inline struct lock_class *
register_lock_class(struct lockdep_map *lock, unsigned int subclass, int force)
{
	struct lockdep_subclass_key *key;
	struct list_head *hash_head;
	struct lock_class *class;
	unsigned long flags;

	class = look_up_lock_class(lock, subclass);
	if (likely(class))
		return class;

	/*
	 * Debug-check: all keys must be persistent!
 	 */
	if (!static_obj(lock->key)) {
		debug_locks_off();
		printk("INFO: trying to register non-static key.\n");
		printk("the code is fine but needs lockdep annotation.\n");
		printk("turning off the locking correctness validator.\n");
		dump_stack();
		printk("name %s\n", lock->name);
		printk("key %p\n", lock->key);

		return NULL;
	}

	/*
	 * Debug-check: the name needs to be no null!
 	 */
	if (!lock->name) {
		printk("INFO: trying to register lock with no name.\n");
		dump_stack();
		printk("name %s\n", lock->name);
		printk("key %p\n", lock->key);

		return NULL;
	}


	key = lock->key->subkeys + subclass;
	hash_head = classhashentry(key);

	raw_local_irq_save(flags);
	if (!graph_lock()) {
		raw_local_irq_restore(flags);
		return NULL;
	}
	/*
	 * We have to do the hash-walk again, to avoid races
	 * with another CPU:
	 */
	list_for_each_entry(class, hash_head, hash_entry)
		if (class->key == key)
			goto out_unlock_set;
	/*
	 * Allocate a new key from the static array, and add it to
	 * the hash:
	 */
	if (nr_lock_classes >= MAX_LOCKDEP_KEYS) {
		if (!debug_locks_off_graph_unlock()) {
			raw_local_irq_restore(flags);
			return NULL;
		}
		raw_local_irq_restore(flags);

		printk("BUG: MAX_LOCKDEP_KEYS too low!\n");
		printk("turning off the locking correctness validator.\n");
		dump_stack();
		return NULL;
	}
	class = lock_classes + nr_lock_classes++;
	debug_atomic_inc(nr_unused_locks);
	class->key = key;
	class->name = lock->name;
	class->subclass = subclass;
	INIT_LIST_HEAD(&class->lock_entry);
	INIT_LIST_HEAD(&class->locks_before);
	INIT_LIST_HEAD(&class->locks_after);
	class->name_version = count_matching_names(class);
	/*
	 * We use RCU's safe list-add method to make
	 * parallel walking of the hash-list safe:
	 */
	list_add_tail_rcu(&class->hash_entry, hash_head);
	/*
	 * Add it to the global list of classes:
	 */
	list_add_tail_rcu(&class->lock_entry, &all_lock_classes);

	/*
	if (verbose(class)) {
		graph_unlock();
		raw_local_irq_restore(flags);

		printk("\nnew class %p: %s", class->key, class->name);
		if (class->name_version > 1)
			printk("#%d", class->name_version);
		printk("\n");
		dump_stack();

		raw_local_irq_save(flags);
		if (!graph_lock()) {
			raw_local_irq_restore(flags);
			return NULL;
		}
	}
	*/
out_unlock_set:
	graph_unlock();
	raw_local_irq_restore(flags);

	if (!subclass || force)
		lock->class_cache = class;

	if (DEBUG_LOCKS_WARN_ON(class->subclass != subclass))
		return NULL;

	return class;
}

extern int lockstat_lock_acquire(struct lockdep_map *lock, unsigned int subclass,
			  int trylock, int read, int hardirqs_off,
			  struct lockdep_map *nest_lock, unsigned long ip,
			  int references);

/*
 * Remove the lock to the list of currently held locks in a
 * potentially non-nested (out of order) manner. This is a
 * relatively rare operation, as all the unlock APIs default
 * to nested mode (which uses lock_release()):
 */
int
lock_release_non_nested(struct task_struct *curr,
			struct lockdep_map *lock, unsigned long ip)
{
	struct held_lock *hlock, *prev_hlock;
	unsigned int depth;
	int i;

#ifdef CONFIG_DEBUG_LOCK_HOOKS
	atomic_inc(&ls_non_nested);
#endif

	/*
	 * Check whether the lock exists in the current stack
	 * of held locks:
	 */
	depth = curr->lockdep_depth;
	if (DEBUG_LOCKS_WARN_ON(!depth))
		return 0;

	prev_hlock = NULL;
	for (i = depth-1; i >= 0; i--) {
		hlock = curr->held_locks + i;
		/*
		 * We must not cross into another context:
		 */
		if (prev_hlock && prev_hlock->irq_context != hlock->irq_context)
			break;
		if (match_held_lock(hlock, lock))
			goto found_it;
		prev_hlock = hlock;
	}
	//return print_unlock_inbalance_bug(curr, lock, ip);
	return 0;

found_it:
	if (hlock->instance == lock)
		lock_release_holdtime(hlock);

	if (hlock->references) {
		hlock->references--;
		if (hlock->references) {
			/*
			 * We had, and after removing one, still have
			 * references, the current lock stack is still
			 * valid. We're done!
			 */
			return 1;
		}
	}

	/*
	 * We have the right lock to unlock, 'hlock' points to it.
	 * Now we remove it from the stack, and add back the other
	 * entries (if any), recalculating the hash along the way:
	 */

	curr->lockdep_depth = i;
	curr->curr_chain_key = hlock->prev_chain_key;

	// AP: XXX
	for (i++; i < depth; i++) {
		hlock = curr->held_locks + i;
		if (!lockstat_lock_acquire(hlock->instance,
			hlock_class(hlock)->subclass, hlock->trylock,
				hlock->read, hlock->hardirqs_off,
				hlock->nest_lock, hlock->acquire_ip,
				hlock->references))
			return 0;
	}

	if (DEBUG_LOCKS_WARN_ON(curr->lockdep_depth != depth - 1))
		return 0;
	return 1;
}

/*
 * Remove the lock to the list of currently held locks - this gets
 * called on mutex_unlock()/spin_unlock*() (or on a failed
 * mutex_lock_interruptible()). This is done for unlocks that nest
 * perfectly. (i.e. the current top of the lock-stack is unlocked)
 */
int lock_release_nested(struct task_struct *curr,
			       struct lockdep_map *lock, unsigned long ip)
{
	struct held_lock *hlock;
	unsigned int depth;

#ifdef CONFIG_DEBUG_LOCK_HOOKS
	atomic_inc(&ls_nested);
#endif

	/*
	 * Pop off the top of the lock stack:
	 */
	depth = curr->lockdep_depth - 1;
	hlock = curr->held_locks + depth;

	/*
	 * Is the unlock non-nested:
	 */
	if (hlock->instance != lock || hlock->references)
		return lock_release_non_nested(curr, lock, ip);
	curr->lockdep_depth--;

	if (DEBUG_LOCKS_WARN_ON(!depth && (hlock->prev_chain_key != 0)))
		return 0;

	curr->curr_chain_key = hlock->prev_chain_key;

	lock_release_holdtime(hlock);

#ifdef CONFIG_DEBUG_LOCKDEP
	hlock->prev_chain_key = 0;
	hlock->class_idx = 0;
	hlock->acquire_ip = 0;
	hlock->irq_context = 0;
#endif
	return 1;
}

void print_lockdep_cache(struct lockdep_map *lock)
{
	const char *name;
	char str[KSYM_NAME_LEN];

	name = lock->name;
	if (!name)
		name = __get_key_name(lock->key->subkeys, str);

	printk("%s", name);
}
