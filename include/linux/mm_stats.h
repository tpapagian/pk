#ifndef __LINUX_MM_STATS_H
#define __LINUX_MM_STATS_H

#define AMDRAGON_FOR_MM_STAT(__x)		\
	__x(mmap_cache_hit)			\
	__x(pf_count)				\
	__x(pf_find_vma_cycles)			\
	__x(pf_run_cycles)			\
	__x(pf_wall_cycles)

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
