#ifndef _LINUX_SMP_LIST_H
#define _LINUX_SMP_LIST_H

#include <linux/list.h>
#include <linux/spinlock.h>

struct smp_list {
	struct list_head	list;
	spinlock_t		lock;
	char			__pad[0] ____cacheline_aligned_in_smp;
} ____cacheline_aligned_in_smp;

static inline void smp_list_init(struct smp_list *l)
{
	INIT_LIST_HEAD(&l->list);
	spin_lock_init(&l->lock);
}

#endif
