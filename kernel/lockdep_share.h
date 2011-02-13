#ifndef __LINUX_LOCKDEP_SHARE_H
#define __LINUX_LOCKDEP_SHARE_H

#include <linux/debug_locks.h>
#include <linux/kallsyms.h>

extern struct lock_class lock_classes[MAX_LOCKDEP_KEYS];
extern void print_lockdep_cache(struct lockdep_map *lock);
extern void lockdep_print_held_locks(struct task_struct *curr);
extern int match_held_lock(struct held_lock *hlock, struct lockdep_map *lock);
extern void check_flags(unsigned long flags);

extern int static_obj(void *obj);

extern unsigned int max_lockdep_depth;

static inline struct lock_class *hlock_class(struct held_lock *hlock)
{
	if (!hlock->class_idx) {
		DEBUG_LOCKS_WARN_ON(1);
		return NULL;
	}
	return lock_classes + hlock->class_idx - 1;
}

static inline const char * __get_key_name(struct lockdep_subclass_key *key, char *str)
{
	return kallsyms_lookup((unsigned long)key, NULL, NULL, NULL, str);
}

/*
 * The lockdep classes are in a hash-table as well, for fast lookup:
 */
#define CLASSHASH_BITS		(MAX_LOCKDEP_KEYS_BITS - 1)
#define CLASSHASH_SIZE		(1UL << CLASSHASH_BITS)
#define __classhashfn(key)	hash_long((unsigned long)key, CLASSHASH_BITS)
#define classhashentry(key)	(classhash_table + __classhashfn((key)))

extern struct list_head classhash_table[CLASSHASH_SIZE];

extern inline struct lock_class *look_up_lock_class(struct lockdep_map *lock,
		unsigned int subclass);

extern inline struct lock_class *register_lock_class(struct lockdep_map *lock,
		unsigned int subclass, int force);

extern int lock_release_non_nested(struct task_struct *curr,
		struct lockdep_map *lock, unsigned long ip);

extern int lock_release_nested(struct task_struct *curr,
			       struct lockdep_map *lock, unsigned long ip);

#endif /* __LINUX_LOCKDEP_SHARE_H */
