#include <linux/spinlock.h>
#include <linux/module.h>

void __raw_spin_lock_init(raw_spinlock_t *lock, const char *name,
			  struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC // AP: XXX why this one???
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
#endif

#ifdef CONFIG_LOCK_DEBUG_HOOKS
	lockdep_init_map(&lock->dep_map, name, key, 0);
#endif

	lock->raw_lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;

// AP: TODO it would be best if this was a function call to code that deals with this.
#ifdef CONFIG_DEBUG_SPINLOCK
	lock->magic = SPINLOCK_MAGIC;
	lock->owner = SPINLOCK_OWNER_INIT;
	lock->owner_cpu = -1;
#endif
}
EXPORT_SYMBOL(__raw_spin_lock_init);

void __rwlock_init(rwlock_t *lock, const char *name,
		   struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
#endif

#ifdef CONFIG_LOCK_DEBUG_HOOKS
	lockdep_init_map(&lock->dep_map, name, key, 0);
#endif

	lock->raw_lock = (arch_rwlock_t) __ARCH_RW_LOCK_UNLOCKED;

// AP: TODO it would be best if this was a function call to code that deals with this.
#ifdef CONFIG_DEBUG_SPINLOCK
	lock->magic = RWLOCK_MAGIC;
	lock->owner = SPINLOCK_OWNER_INIT;
	lock->owner_cpu = -1;
#endif
}

EXPORT_SYMBOL(__rwlock_init);
