#ifndef __LINUX_LOCK_DEBUG_HOOKS_H
#define __LINUX_LOCK_DEBUG_HOOKS_H

#include <linux/list.h>
#include <linux/lockstat2.h>

#include <linux/stacktrace.h>

//AP: XXX a struct lockdep_map is too specific for this generic hooks code,
//    maybe have a struct lock_hooks instead with struct lockdep_map a member.

#define MAX_LOCKDEP_KEYS_BITS		13
/*
 * Subtract one because we offset hlock->class_idx by 1 in order
 * to make 0 mean no class. This avoids overflowing the class_idx
 * bitfield and hitting the BUG in hlock_class().
 */
#define MAX_LOCKDEP_KEYS		((1UL << MAX_LOCKDEP_KEYS_BITS) - 1)

#define MAX_LOCKDEP_SUBCLASSES		8UL

/*
 * We'd rather not expose kernel/lockdep_states.h this wide, but we do need
 * the total number of states... :-(
 */
#define XXX_LOCK_USAGE_STATES		(1+3*4)

#define LOCKSTAT_POINTS		4

/*
 * The lock-class itself:
 */
struct lock_class {
	/*
	 * class-hash:
	 */
	struct list_head		hash_entry;

	/*
	 * global list of all lock-classes:
	 */
	struct list_head		lock_entry;

	struct lockdep_subclass_key	*key;
	unsigned int			subclass;
	unsigned int			dep_gen_id;

	/*
	 * IRQ/softirq usage tracking bits:
	 */
	unsigned long			usage_mask;
	struct stack_trace		usage_traces[XXX_LOCK_USAGE_STATES];

	/*
	 * These fields represent a directed graph of lock dependencies,
	 * to every node we attach a list of "forward" and a list of
	 * "backward" graph nodes.
	 */
	struct list_head		locks_after, locks_before;

	/*
	 * Generation counter, when doing certain classes of graph walking,
	 * to ensure that we check one node only once:
	 */
	unsigned int			version;

	/*
	 * Statistics counter:
	 */
	unsigned long			ops;

	const char			*name;
	int				name_version;

#ifdef CONFIG_LOCK_STAT2
	unsigned long			contention_point[LOCKSTAT_POINTS];
	unsigned long			contending_point[LOCKSTAT_POINTS];
#endif
};

extern unsigned long nr_lock_classes;
extern struct lock_class lock_classes[MAX_LOCKDEP_KEYS];

/*
 * Lock-classes are keyed via unique addresses, by embedding the
 * lockclass-key into the kernel (or module) .data section. (For
 * static locks we use the lock address itself as the key.)
 */
struct lockdep_subclass_key {
	char __one_byte;
} __attribute__ ((__packed__));

struct lock_class_key {
	struct lockdep_subclass_key	subkeys[MAX_LOCKDEP_SUBCLASSES];
};

/*
 * Map the lock object (the lock instance) to the lock-class object.
 * This is embedded into specific lock instances:
 */
struct lockdep_map {
	struct lock_class_key		*key;
	struct lock_class		*class_cache;
	const char			*name;
#ifdef CONFIG_LOCK_STAT2
	int				cpu;
	unsigned long			ip;
#endif
};

struct held_lock {
	/*
	 * One-way hash of the dependency chain up to this point. We
	 * hash the hashes step by step as the dependency chain grows.
	 *
	 * We use it for dependency-caching and we skip detection
	 * passes and dependency-updates if there is a cache-hit, so
	 * it is absolutely critical for 100% coverage of the validator
	 * to have a unique key value for every unique dependency path
	 * that can occur in the system, to make a unique hash value
	 * as likely as possible - hence the 64-bit width.
	 *
	 * The task struct holds the current hash value (initialized
	 * with zero), here we store the previous hash value:
	 */
	u64				prev_chain_key;
	unsigned long			acquire_ip;
	struct lockdep_map		*instance;
	struct lockdep_map		*nest_lock;
#ifdef CONFIG_LOCK_STAT2
	u64 				waittime_stamp;
	u64				holdtime_stamp;
#endif
	unsigned int			class_idx:MAX_LOCKDEP_KEYS_BITS;
	/*
	 * The lock-stack is unified in that the lock chains of interrupt
	 * contexts nest ontop of process context chains, but we 'separate'
	 * the hashes by starting with 0 if we cross into an interrupt
	 * context, and we also keep do not add cross-context lock
	 * dependencies - the lock usage graph walking covers that area
	 * anyway, and we'd just unnecessarily increase the number of
	 * dependencies otherwise. [Note: hardirq and softirq contexts
	 * are separated from each other too.]
	 *
	 * The following field is used to detect when we cross into an
	 * interrupt context:
	 */
	unsigned int irq_context:2; /* bit 0 - soft, bit 1 - hard */
	unsigned int trylock:1;						/* 16 bits */

	unsigned int read:2;        /* see lock_acquire() comment */
	unsigned int check:2;       /* see lock_acquire() comment */
	unsigned int hardirqs_off:1;
	unsigned int references:11;					/* 32 bits */
};

/*
 * To initialize a lockdep_map statically use this macro.
 * Note that _name must not be NULL.
 */
#define STATIC_LOCKDEP_MAP_INIT(_name, _key) \
	{ .name = (_name), .key = (void *)(_key), }

/*
 * Map the dependency ops to NOP or to real lockdep ops, depending
 * on the per lock-class debug mode:
 */

#ifdef CONFIG_LOCK_DEBUG_HOOKS
# ifdef CONFIG_PROVE_LOCKING
#  define spin_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 2, NULL, i)
#  define spin_acquire_nest(l, s, t, n, i)	lock_acquire(l, s, t, 0, 2, n, i)
# else
#  define spin_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 1, NULL, i)
#  define spin_acquire_nest(l, s, t, n, i)	lock_acquire(l, s, t, 0, 1, NULL, i)
# endif
# define spin_release(l, n, i)			lock_release(l, n, i)
#else
# define spin_acquire(l, s, t, i)		do { } while (0)
# define spin_release(l, n, i)			do { } while (0)
#endif

#ifdef CONFIG_LOCK_DEBUG_HOOKS
# ifdef CONFIG_PROVE_LOCKING
#  define rwlock_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 2, NULL, i)
#  define rwlock_acquire_read(l, s, t, i)	lock_acquire(l, s, t, 2, 2, NULL, i)
# else
#  define rwlock_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 1, NULL, i)
#  define rwlock_acquire_read(l, s, t, i)	lock_acquire(l, s, t, 2, 1, NULL, i)
# endif
# define rwlock_release(l, n, i)		lock_release(l, n, i)
#else
# define rwlock_acquire(l, s, t, i)		do { } while (0)
# define rwlock_acquire_read(l, s, t, i)	do { } while (0)
# define rwlock_release(l, n, i)		do { } while (0)
#endif

#ifdef CONFIG_LOCK_DEBUG_HOOKS
# ifdef CONFIG_PROVE_LOCKING
#  define mutex_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 2, NULL, i)
# else
#  define mutex_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 1, NULL, i)
# endif
# define mutex_release(l, n, i)			lock_release(l, n, i)
#else
# define mutex_acquire(l, s, t, i)		do { } while (0)
# define mutex_release(l, n, i)			do { } while (0)
#endif

#ifdef CONFIG_LOCK_DEBUG_HOOKS
# ifdef CONFIG_PROVE_LOCKING
#  define rwsem_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 2, NULL, i)
#  define rwsem_acquire_read(l, s, t, i)	lock_acquire(l, s, t, 1, 2, NULL, i)
# else
#  define rwsem_acquire(l, s, t, i)		lock_acquire(l, s, t, 0, 1, NULL, i)
#  define rwsem_acquire_read(l, s, t, i)	lock_acquire(l, s, t, 1, 1, NULL, i)
# endif
# define rwsem_release(l, n, i)			lock_release(l, n, i)
#else
# define rwsem_acquire(l, s, t, i)		do { } while (0)
# define rwsem_acquire_read(l, s, t, i)		do { } while (0)
# define rwsem_release(l, n, i)			do { } while (0)
#endif

#ifdef CONFIG_LOCK_DEBUG_HOOKS
# ifdef CONFIG_PROVE_LOCKING
#  define lock_map_acquire(l)		lock_acquire(l, 0, 0, 0, 2, NULL, _THIS_IP_)
# else
#  define lock_map_acquire(l)		lock_acquire(l, 0, 0, 0, 1, NULL, _THIS_IP_)
# endif
# define lock_map_release(l)			lock_release(l, 1, _THIS_IP_)
#else
# define lock_map_acquire(l)			do { } while (0)
# define lock_map_release(l)			do { } while (0)
#endif

#ifdef CONFIG_LOCK_DEBUG_HOOKS

/*
 * Acquire a lock.
 *
 * Values for "read":
 *
 *   0: exclusive (write) acquire
 *   1: read-acquire (no recursion allowed)
 *   2: read-acquire with same-instance recursion allowed
 *
 * Values for check:
 *
 *   0: disabled
 *   1: simple checks (freeing, held-at-exit-time, etc.)
 *   2: full validation
 */
extern void lock_acquire(struct lockdep_map *lock, unsigned int subclass,
			 int trylock, int read, int check,
			 struct lockdep_map *nest_lock, unsigned long ip);

extern void lock_release(struct lockdep_map *lock, int nested,
			 unsigned long ip);
#else /* CONFIG_LOCK_DEBUG_HOOKS */

# define lock_acquire(l, s, t, r, c, n, i)	do { } while (0)
# define lock_release(l, n, i)			do { } while (0)

#endif /* !CONFIG_LOCK_DEBUG_HOOKS */

#ifdef CONFIG_LOCK_DEBUG_HOOKS

extern void lock_contended(struct lockdep_map *lock, unsigned long ip);
extern void lock_acquired(struct lockdep_map *lock, unsigned long ip);

#define LOCK_CONTENDED(_lock, try, lock)			\
do {								\
	if (!try(_lock)) {					\
		lock_contended(&(_lock)->dep_map, _RET_IP_);	\
		lock(_lock);					\
	}							\
	lock_acquired(&(_lock)->dep_map, _RET_IP_);		\
} while (0)

#else /* CONFIG_LOCK_DEBUG_HOOKS */

#define lock_contended(lockdep_map, ip) do {} while (0)
#define lock_acquired(lockdep_map, ip) do {} while (0)

#define LOCK_CONTENDED(_lock, try, lock) \
	lock(_lock)

#endif /* CONFIG_LOCK_DEBUG_HOOKS */

#ifdef CONFIG_LOCK_DEBUG_HOOKS

/*
 * AP: XXX had to remove the parentheses  around try and lock because that was
 * causing the preprocessor to generate this code:
 * (macro_function_name)(argument). The preprocessor has no idea how to parse
 * this and decides that macro_function_name does not exist even though
 * macro_function_name(some_args...) does exist.
 */

/*
 * On lockdep we dont want the hand-coded irq-enable of
 * _raw_*_lock_flags() code, because lockdep assumes
 * that interrupts are not re-enabled during lock-acquire:
 */
#define LOCK_CONTENDED_FLAGS(_lock, try, lock, lockfl, flags) \
	LOCK_CONTENDED((_lock), try, lock)

#else /* CONFIG_LOCK_DEBUG_HOOKS */

#define LOCK_CONTENDED_FLAGS(_lock, try, lock, lockfl, flags) \
	lockfl((_lock), (flags))

#endif /* CONFIG_LOCK_DEBUG_HOOKS */

#ifdef CONFIG_GENERIC_HARDIRQS
extern void early_init_irq_lock_class(void);
#else
static inline void early_init_irq_lock_class(void)
{
}
#endif

#endif /* __LINUX_LOCK_DEBUG_HOOKS_H */
