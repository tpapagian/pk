extern struct lock_class lock_classes[MAX_LOCKDEP_KEYS];
extern void print_lockdep_cache(struct lockdep_map *lock);
extern void lockdep_print_held_locks(struct task_struct *curr);
extern int match_held_lock(struct held_lock *hlock, struct lockdep_map *lock);
extern void check_flags(unsigned long flags);

static inline struct lock_class *hlock_class(struct held_lock *hlock)
{
	if (!hlock->class_idx) {
		DEBUG_LOCKS_WARN_ON(1);
		return NULL;
	}
	return lock_classes + hlock->class_idx - 1;
}
