#include <linux/mutex.h>
#include <linux/lock_debug_hooks.h>

void debug_mutex_init(struct mutex *lock, const char *name,
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

#ifdef CONFIG_DEBUG_MUTEXES
	lock->magic = lock;
#endif
}

