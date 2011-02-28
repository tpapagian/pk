#include <linux/mm_types.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/cpumask.h>
#include <linux/mm.h>
#include <linux/mm_lock.h>

#include <asm/atomic.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>

#ifndef INIT_MM_CONTEXT
#define INIT_MM_CONTEXT(name)
#endif

struct mm_struct init_mm = {
	INIT_MM_ROOT,
	.pgd		= swapper_pg_dir,
	.mm_users	= ATOMIC_INIT(2),
	.mm_count	= ATOMIC_INIT(1),
	INIT_MM_LOCK(init_mm),
	.page_table_lock =  __SPIN_LOCK_UNLOCKED(init_mm.page_table_lock),
	.mmlist		= LIST_HEAD_INIT(init_mm.mmlist),
	.cpu_vm_mask	= CPU_MASK_ALL,
	INIT_MM_CONTEXT(init_mm)
};
