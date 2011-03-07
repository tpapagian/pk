#ifndef __LINUX_MM_STATS_H
#define __LINUX_MM_STATS_H

#define AMDRAGON_FOR_MM_STAT(__x)		\
	__x(mmap_cache_hit)			\
	__x(pud_alloc)				\
	__x(pud_alloc_race)			\
	__x(pmd_alloc)				\
	__x(pmd_alloc_race)			\
	__x(pte_alloc)				\
	__x(pte_alloc_race)			\
	__x(mmap_count)				\
	__x(mmap_run_cycles)			\
	__x(mmap_wall_cycles)			\
	__x(munmap_count)			\
	__x(munmap_run_cycles)			\
	__x(munmap_wall_cycles)			\
	__x(pf_find_vma_cycles)			\
	__x(pf_count)				\
	__x(pf_run_cycles)			\
	__x(pf_wall_cycles)			\
	__x(unmap_races)			\
	__x(anon_vma_retries)			\
	__x(stack_guard_retries)		\
	__x(type_retries)			\
	__x(oob_retries)			\
	__x(reuse_vma)				\
	__x(reuse_vma_try_expand)		\
	__x(reuse_vma_fail)			\
	DO_STATS_MMAP_CACHE_RACE(__x)		\
	DO_STATS_TREE_LOCK(__x)

#ifdef CONFIG_AMDRAGON_MMAP_CACHE_RACE
#define DO_STATS_MMAP_CACHE_RACE(__x)	    \
	__x(mmap_cache_pf_shootdowns)	    \
	__x(mmap_cache_find_vma_shootdowns)
#else
#define DO_STATS_MMAP_CACHE_RACE(__x)
#endif

#ifdef CONFIG_AMDRAGON_SPLIT_TREE_LOCK
#define DO_STATS_TREE_LOCK(__x)			\
	__x(tree_lock_read_contended)		\
	__x(tree_lock_read_contended_cycles)	\
	__x(tree_lock_read_uncontended)		\
	__x(tree_lock_read_uncontended_cycles)	\
	__x(tree_lock_read_release_cycles)
#else
#define DO_STATS_TREE_LOCK(__x)
#endif

#ifdef CONFIG_AMDRAGON_MM_STATS

#define DECLARE_MM_STAT(stat)					\
	DECLARE_PER_CPU(unsigned long long, mm_stat_##stat);
AMDRAGON_FOR_MM_STAT(DECLARE_MM_STAT)
#undef DECLARE_MM_STAT

#define AMDRAGON_MM_STAT_ADD(stat, val)				\
	do { percpu_add(mm_stat_##stat, val); } while (0)

struct mm_stat_time
{
	cycles_t start_tsc, start_run_tsc;
};

// Note that the wall_cycles statistics are only valid for
// architectures with synchronized TSC's.  run_cycles should always be
// valid.
#define AMDRAGON_MM_STAT_TIME(stat_time, tsk)				\
	do {								\
		(stat_time)->start_tsc = get_cycles();			\
		(stat_time)->start_run_tsc =				\
			(tsk)->run_accum + ((stat_time)->start_tsc -	\
					    (tsk)->last_run_start);	\
	} while (0)

#define AMDRAGON_MM_STAT_TIME_END(stat_time, tsk, name)			\
	do {								\
		cycles_t __end_tsc = get_cycles();			\
		cycles_t __end_run_tsc =				\
			(tsk)->run_accum + (__end_tsc -			\
					    (tsk)->last_run_start);	\
		AMDRAGON_MM_STAT_ADD(name##_wall_cycles,		\
				     __end_tsc - (stat_time)->start_tsc); \
		AMDRAGON_MM_STAT_ADD(name##_run_cycles,			\
				     __end_run_tsc - (stat_time)->start_run_tsc); \
		AMDRAGON_MM_STAT_INC(name##_count);			\
	} while (0)

#else

#define AMDRAGON_MM_STAT_ADD(stat, val) do { } while (0)

struct mm_stat_time { };
#define AMDRAGON_MM_STAT_TIME(stat_time, tsk) do { } while (0)
#define AMDRAGON_MM_STAT_TIME_END(stat_time, tsk) do { } while (0)

#endif

#define AMDRAGON_MM_STAT_INC(stat) AMDRAGON_MM_STAT_ADD(stat, 1)

#endif /* __LINUX_MM_STATS_H */
