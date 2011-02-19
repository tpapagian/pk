#ifdef CONFIG_LOCK_STAT2

// AP: XXX this should be lockstat2.h

#include <linux/sched.h>

extern void lock_release_holdtime(struct held_lock *hlock);

static inline u64 lockstat_clock(void)
{
	return cpu_clock(smp_processor_id());
}

#else
static inline void lock_release_holdtime(struct held_lock *hlock)
{
}
#endif
