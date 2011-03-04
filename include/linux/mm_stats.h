#ifndef __LINUX_MM_STATS_H
#define __LINUX_MM_STATS_H

#define AMDRAGON_FOR_MM_STAT(__x)		\
	__x(mmap_cache_hit)			\
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

#else

#define AMDRAGON_MM_STAT_ADD(stat, val) do { } while (0)

#endif

#define AMDRAGON_MM_STAT_INC(stat) AMDRAGON_MM_STAT_ADD(stat, 1)

#endif /* __LINUX_MM_STATS_H */
