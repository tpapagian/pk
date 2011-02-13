#define DISABLE_BRANCH_PROFILING
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

#include <linux/lock_debug_hooks.h>
#include <linux/lockstat2.h>

#include "lockdep_internals.h"

#include <asm/sections.h>

int lock_stat = 1;
module_param(lock_stat, int, 0644);

/*
 * We keep a global list of all lock classes. The list only grows,
 * never shrinks. The list is only accessed with the lockdep
 * spinlock lock held.
 */
LIST_HEAD(all_lock_classes);

struct list_head classhash_table[CLASSHASH_SIZE];

static DEFINE_PER_CPU(struct lock_class_stats[MAX_LOCKDEP_KEYS],
		      cpu_lock_stats);

#ifdef CONFIG_DEBUG_LOCK_HOOKS
atomic_t ls_acquire;
atomic_t ls_release;
atomic_t ls_contended;
atomic_t ls_acquired;
atomic_t ls_holdtime;
#endif

static inline u64 lockstat_clock(void)
{
	return cpu_clock(smp_processor_id());
}

static int lock_point(unsigned long points[], unsigned long ip)
{
	int i;

	for (i = 0; i < LOCKSTAT_POINTS; i++) {
		if (points[i] == 0) {
			points[i] = ip;
			break;
		}
		if (points[i] == ip)
			break;
	}

	return i;
}

static void lock_time_inc(struct lock_time *lt, u64 time)
{
	if (time > lt->max)
		lt->max = time;

	if (time < lt->min || !lt->nr)
		lt->min = time;

	lt->total += time;
	lt->nr++;
}

static inline void lock_time_add(struct lock_time *src, struct lock_time *dst)
{
	if (!src->nr)
		return;

	if (src->max > dst->max)
		dst->max = src->max;

	if (src->min < dst->min || !dst->nr)
		dst->min = src->min;

	dst->total += src->total;
	dst->nr += src->nr;
}

struct lock_class_stats lock_stats(struct lock_class *class)
{
	struct lock_class_stats stats;
	int cpu, i;

	memset(&stats, 0, sizeof(struct lock_class_stats));
	for_each_possible_cpu(cpu) {
		struct lock_class_stats *pcs =
			&per_cpu(cpu_lock_stats, cpu)[class - lock_classes];

		for (i = 0; i < ARRAY_SIZE(stats.contention_point); i++)
			stats.contention_point[i] += pcs->contention_point[i];

		for (i = 0; i < ARRAY_SIZE(stats.contending_point); i++)
			stats.contending_point[i] += pcs->contending_point[i];

		lock_time_add(&pcs->read_waittime, &stats.read_waittime);
		lock_time_add(&pcs->write_waittime, &stats.write_waittime);

		lock_time_add(&pcs->read_holdtime, &stats.read_holdtime);
		lock_time_add(&pcs->write_holdtime, &stats.write_holdtime);

		for (i = 0; i < ARRAY_SIZE(stats.bounces); i++)
			stats.bounces[i] += pcs->bounces[i];
	}

	return stats;
}

void clear_lock_stats(struct lock_class *class)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct lock_class_stats *cpu_stats =
			&per_cpu(cpu_lock_stats, cpu)[class - lock_classes];

		memset(cpu_stats, 0, sizeof(struct lock_class_stats));
	}
	memset(class->contention_point, 0, sizeof(class->contention_point));
	memset(class->contending_point, 0, sizeof(class->contending_point));
}

static struct lock_class_stats *get_lock_stats(struct lock_class *class)
{
	return &get_cpu_var(cpu_lock_stats)[class - lock_classes];
}

static void put_lock_stats(struct lock_class_stats *stats)
{
	put_cpu_var(cpu_lock_stats);
}

void lock_release_holdtime(struct held_lock *hlock)
{
	struct lock_class_stats *stats;
	u64 holdtime;

#ifdef CONFIG_DEBUG_LOCK_HOOKS
	atomic_inc(&ls_holdtime);
#endif

	if (!lock_stat)
		return;

	holdtime = lockstat_clock() - hlock->holdtime_stamp;

	stats = get_lock_stats(hlock_class(hlock));
	if (hlock->read)
		lock_time_inc(&stats->read_holdtime, holdtime);
	else
		lock_time_inc(&stats->write_holdtime, holdtime);
	put_lock_stats(stats);
}

/*
 * Locking printouts:
 */

#define __USAGE(__STATE)						\
	[LOCK_USED_IN_##__STATE] = "IN-"__stringify(__STATE)"-W",	\
	[LOCK_ENABLED_##__STATE] = __stringify(__STATE)"-ON-W",		\
	[LOCK_USED_IN_##__STATE##_READ] = "IN-"__stringify(__STATE)"-R",\
	[LOCK_ENABLED_##__STATE##_READ] = __stringify(__STATE)"-ON-R",

static inline unsigned long lock_flag(enum lock_usage_bit bit)
{
	return 1UL << bit;
}

static char get_usage_char(struct lock_class *class, enum lock_usage_bit bit)
{
	char c = '.';

	if (class->usage_mask & lock_flag(bit + 2))
		c = '+';
	if (class->usage_mask & lock_flag(bit)) {
		c = '-';
		if (class->usage_mask & lock_flag(bit + 2))
			c = '?';
	}

	return c;
}

void get_usage_chars(struct lock_class *class, char usage[LOCK_USAGE_CHARS])
{
	int i = 0;

#define LOCKDEP_STATE(__STATE) 						\
	usage[i++] = get_usage_char(class, LOCK_USED_IN_##__STATE);	\
	usage[i++] = get_usage_char(class, LOCK_USED_IN_##__STATE##_READ);
#include "lockdep_states.h"
#undef LOCKDEP_STATE

	usage[i] = '\0';
}

static void print_lock_name(struct lock_class *class)
{
	char str[KSYM_NAME_LEN], usage[LOCK_USAGE_CHARS];
	const char *name;

	get_usage_chars(class, usage);

	name = class->name;
	if (!name) {
		name = __get_key_name(class->key, str);
		printk(" (%s", name);
	} else {
		printk(" (%s", name);
		if (class->name_version > 1)
			printk("#%d", class->name_version);
		if (class->subclass)
			printk("/%d", class->subclass);
	}
	printk("){%s}", usage);
}

static void print_lock(struct held_lock *hlock)
{
	print_lock_name(hlock_class(hlock));
	printk(", at: ");
	print_ip_sym(hlock->acquire_ip);
}

void lockdep_print_held_locks(struct task_struct *curr)
{
	int i, depth = curr->lockdep_depth;

	if (!depth) {
		printk("no locks held by %s/%d.\n", curr->comm, task_pid_nr(curr));
		return;
	}
	printk("%d lock%s held by %s/%d:\n",
		depth, depth > 1 ? "s" : "", curr->comm, task_pid_nr(curr));

	for (i = 0; i < depth; i++) {
		printk(" #%d: ", i);
		print_lock(curr->held_locks + i);
	}
}

static int
print_lock_contention_bug(struct task_struct *curr, struct lockdep_map *lock,
			   unsigned long ip)
{
	if (!debug_locks_off())
		return 0;
	if (debug_locks_silent)
		return 0;

	printk("\n=================================\n");
	printk(  "[ BUG: bad contention detected! ]\n");
	printk(  "---------------------------------\n");
	printk("%s/%d is trying to contend lock (",
		curr->comm, task_pid_nr(curr));
	print_lockdep_cache(lock);
	printk(") at:\n");
	print_ip_sym(ip);
	printk("but there are no locks held!\n");
	printk("\nother info that might help us debug this:\n");
	lockdep_print_held_locks(curr);

	printk("\nstack backtrace:\n");
	dump_stack();

	return 0;
}

int match_held_lock(struct held_lock *hlock, struct lockdep_map *lock)
{
	if (hlock->instance == lock)
		return 1;

	if (hlock->references) {
		struct lock_class *class = lock->class_cache;

		if (!class)
			class = look_up_lock_class(lock, 0);

		if (DEBUG_LOCKS_WARN_ON(!class))
			return 0;

		if (DEBUG_LOCKS_WARN_ON(!hlock->nest_lock))
			return 0;

		if (hlock->class_idx == class - lock_classes + 1)
			return 1;
	}

	return 0;
}

//AP: XXX this needs rewriting
int
lockstat_lock_acquire(struct lockdep_map *lock, unsigned int subclass,
			  int trylock, int read, int hardirqs_off,
			  struct lockdep_map *nest_lock, unsigned long ip,
			  int references)
{
	struct task_struct *curr = current;
	struct lock_class *class = NULL;
	struct held_lock *hlock;
	unsigned int depth;
	int class_idx;

#ifdef CONFIG_DEBUG_LOCK_HOOKS
	atomic_inc(&ls_acquire);
#endif

	if (unlikely(!debug_locks))
		return 0;

	if (DEBUG_LOCKS_WARN_ON(!irqs_disabled()))
		return 0;

	if (unlikely(subclass >= MAX_LOCKDEP_SUBCLASSES)) {
		debug_locks_off();
		printk("BUG: MAX_LOCKDEP_SUBCLASSES too low!\n");
		printk("turning off the locking correctness validator.\n");
		dump_stack();
		return 0;
	}

	if (!subclass)
		class = lock->class_cache;
	/*
	 * Not cached yet or subclass?
	 */
	if (unlikely(!class)) {
		class = register_lock_class(lock, subclass, 0);
		if (!class)
			return 0;
	}
	atomic_inc((atomic_t *)&class->ops); // AP: XXX this is not per-cpu
	/*
	if (very_verbose(class)) {
		printk("\nacquire class [%p] %s", class->key, class->name);
		if (class->name_version > 1)
			printk("#%d", class->name_version);
		printk("\n");
		dump_stack();
	}
	*/

	/*
	 * Add the lock to the list of currently held locks.
	 */
	depth = curr->lockdep_depth;
	if (DEBUG_LOCKS_WARN_ON(depth >= MAX_LOCK_DEPTH))
		return 0;

	class_idx = class - lock_classes + 1;

	if (depth) {
		hlock = curr->held_locks + depth - 1;
		if (hlock->class_idx == class_idx && nest_lock) {
			if (hlock->references)
				hlock->references++;
			else
				hlock->references = 2;

			return 1;
		}
	}

	hlock = curr->held_locks + depth;
	if (DEBUG_LOCKS_WARN_ON(!class))
		return 0;
	hlock->class_idx = class_idx;
	hlock->acquire_ip = ip;
	hlock->instance = lock;
	hlock->nest_lock = nest_lock;
	hlock->trylock = trylock;
	hlock->read = read;
	hlock->hardirqs_off = !!hardirqs_off;
	hlock->references = references;
	hlock->waittime_stamp = 0;
	hlock->holdtime_stamp = lockstat_clock();

	curr->lockdep_depth++;
#ifdef CONFIG_DEBUG_LOCKDEP
	if (unlikely(!debug_locks))
		return 0;
#endif
	if (unlikely(curr->lockdep_depth >= MAX_LOCK_DEPTH)) {
		debug_locks_off();
		printk("BUG: MAX_LOCK_DEPTH too low!\n");
		printk("turning off the locking correctness validator.\n");
		dump_stack();
		return 0;
	}

	return 1;
}

static int
print_unlock_inbalance_bug(struct task_struct *curr, struct lockdep_map *lock,
			   unsigned long ip)
{
	if (!debug_locks_off())
		return 0;
	if (debug_locks_silent)
		return 0;

	printk("\n=====================================\n");
	printk(  "[ BUG: bad unlock balance detected! ]\n");
	printk(  "-------------------------------------\n");
	printk("%s/%d is trying to release lock (",
		curr->comm, task_pid_nr(curr));
	print_lockdep_cache(lock);
	printk(") at:\n");
	print_ip_sym(ip);
	printk("but there are no more locks to release!\n");
	printk("\nother info that might help us debug this:\n");
	lockdep_print_held_locks(curr);

	printk("\nstack backtrace:\n");
	dump_stack();

	return 0;
}

/*
 * Common debugging checks for both nested and non-nested unlock:
 */
static int check_unlock(struct task_struct *curr, struct lockdep_map *lock,
			unsigned long ip)
{
	if (unlikely(!debug_locks))
		return 0;
	if (DEBUG_LOCKS_WARN_ON(!irqs_disabled()))
		return 0;

	if (curr->lockdep_depth <= 0)
		return print_unlock_inbalance_bug(curr, lock, ip);

	return 1;
}

//AP: XXX this needs rewriting
void
lockstat_lock_release(struct lockdep_map *lock, int nested, unsigned long ip)
{
	struct task_struct *curr = current;

#ifdef CONFIG_DEBUG_LOCK_HOOKS
	atomic_inc(&ls_release);
#endif

	if (unlikely(!debug_locks))
		return;

	if (!check_unlock(curr, lock, ip))
		return;

	if (nested) {
		if (!lock_release_nested(curr, lock, ip))
			return;
	} else {
		if (!lock_release_non_nested(curr, lock, ip))
			return;
	}
}

void
lockstat_lock_contended(struct lockdep_map *lock, unsigned long ip)
{
	struct task_struct *curr = current;
	struct held_lock *hlock, *prev_hlock;
	struct lock_class_stats *stats;
	unsigned int depth;
	int i, contention_point, contending_point;

#ifdef CONFIG_DEBUG_LOCK_HOOKS
	atomic_inc(&ls_contended);
#endif

	if (unlikely(!debug_locks))
		return;

	depth = curr->lockdep_depth;
	if (DEBUG_LOCKS_WARN_ON(!depth))
		return;

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
	print_lock_contention_bug(curr, lock, ip);
	return;

found_it:
	if (hlock->instance != lock)
		return;

	hlock->waittime_stamp = lockstat_clock();

	contention_point = lock_point(hlock_class(hlock)->contention_point, ip);
	contending_point = lock_point(hlock_class(hlock)->contending_point,
				      lock->ip);

	stats = get_lock_stats(hlock_class(hlock));
	if (contention_point < LOCKSTAT_POINTS)
		stats->contention_point[contention_point]++;
	if (contending_point < LOCKSTAT_POINTS)
		stats->contending_point[contending_point]++;
	if (lock->cpu != smp_processor_id())
		stats->bounces[bounce_contended + !!hlock->read]++;
	put_lock_stats(stats);
}

void
lockstat_lock_acquired(struct lockdep_map *lock, unsigned long ip)
{
	struct task_struct *curr = current;
	struct held_lock *hlock, *prev_hlock;
	struct lock_class_stats *stats;
	unsigned int depth;
	u64 now, waittime = 0;
	int i, cpu;

#ifdef CONFIG_DEBUG_LOCK_HOOKS
	atomic_inc(&ls_acquired);
#endif

	if (unlikely(!debug_locks))
		return;

	depth = curr->lockdep_depth;
	if (DEBUG_LOCKS_WARN_ON(!depth)) {
		printk("name %s\n", lock->name);
		printk("key %p\n", lock->key);
		return;
	}

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
	print_lock_contention_bug(curr, lock, _RET_IP_);
	return;

found_it:
	if (hlock->instance != lock)
		return;

	cpu = smp_processor_id();
	if (hlock->waittime_stamp) {
		now = lockstat_clock();
		waittime = now - hlock->waittime_stamp;
		hlock->holdtime_stamp = now;
	}

	stats = get_lock_stats(hlock_class(hlock));
	if (waittime) {
		if (hlock->read)
			lock_time_inc(&stats->read_waittime, waittime);
		else
			lock_time_inc(&stats->write_waittime, waittime);
	}
	if (lock->cpu != cpu)
		stats->bounces[bounce_acquired + !!hlock->read]++;
	put_lock_stats(stats);

	lock->cpu = cpu;
	lock->ip = ip;
}
