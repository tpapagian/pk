#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tracepoint.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <trace/events/kmem.h>
#include <trace/events/syscalls.h>
#include <trace/events/sched.h>
#include <trace/events/lock.h>

#include <linux/kprobes.h>
#include <linux/ptrace.h>

#include <asm/traps.h>
#include <asm/vgtod.h>
#include <asm/mtrace-magic.h>

static DEFINE_PER_CPU_ALIGNED(atomic64_t, mtrace_call_tag);
static DEFINE_PER_CPU_ALIGNED(struct mtrace_call_stack, 
			      mtrace_irq_call_stack) = { .curr = -1 };

/* 16 CPUs maximum */
#define MTRACE_CPUS_SHIFT     4UL
#define MTRACE_CPUS_MAX	      (1 << MTRACE_CPUS_SHIFT)
#define MTRACE_TAG_SHIFT      (64UL - MTRACE_CPUS_SHIFT)
#define MTRACE_TAG_MASK       (~((1UL << MTRACE_TAG_SHIFT) - 1UL))

#if MTRACE_CPUS_MAX < NR_CPUS
#error MTRACE_CPUS_MAX < NR_CPUS
#endif

static inline struct kmem_cache *page_get_cache(struct page *page)
{
	page = compound_head(page);
	BUG_ON(!PageSlab(page));
	return (struct kmem_cache *)page->lru.next;
}

static inline struct kmem_cache *virt_to_cache(const void *obj)
{
	struct page *page = virt_to_head_page(obj);
	return page_get_cache(page);
}

static void mtrace_kmem_alloc(void *unsned,
			      unsigned long call_site,
			      const void *ptr,
			      size_t bytes_req,
			      size_t bytes_alloc,
			      gfp_t gfp_flags)
{
	struct kmem_cache *cachep;

	cachep = virt_to_cache(ptr);
	mtrace_label_register(mtrace_label_heap, ptr, bytes_alloc, 
			      cachep->name, strlen(cachep->name), call_site);
}

static void mtrace_kmem_alloc_node(void *unused,
				   unsigned long call_site,
				   const void *ptr,
				   size_t bytes_req,
				   size_t bytes_alloc,
				   gfp_t gfp_flags,
				   int node)
{
	mtrace_kmem_alloc(NULL, call_site, ptr, 
			  bytes_req, bytes_alloc, gfp_flags);
}

static void mtrace_kmem_free(void *unused,
			     unsigned long call_site,
			     const void *ptr)
{
	/* kfree(NULL) is acceptable */
	if (ptr)
		mtrace_label_register(mtrace_label_heap, ptr, 0, NULL, 0, call_site);
}

static void mtrace_mm_page_alloc(void *unused, 
				 struct page *page, 
				 unsigned int order,
				 gfp_t gfp_flags, 
				 int migratetype)
{
    unsigned long length = (1 << order) << PAGE_SHIFT;
    void * va = page_address(page);    

    mtrace_label_register(mtrace_label_block, va, length, 
			  "pages", strlen("pages"), 0);
}


static void mtrace_mm_page_free_direct(void *unused,
				       struct page *page,
				       unsigned int order)
{
    void * va = page_address(page);    

    mtrace_label_register(mtrace_label_block, va, 0, NULL, 0, 0);
}

static void mtrace_mm_pagevec_free(void *unused,
				   struct page *page,
				   int cold)
{
    /* 
     * __pagevec_free ends up calling free_pages_prepare 
     * free_pages_prepare calls trace_mm_page_free_direct
     */
}

static void mtrace_mm_page_alloc_zone_locked(void *unused,
					     struct page *page, 
					     unsigned int order, 
					     int migratetype)
{
    /* 
     * __alloc_pages_nodemask sometimes ends up calling 
     * trace_mm_page_alloc_zone_locked, but it always calls
     * trace_mm_page_alloc
     */
}

static void mtrace_mm_page_pcpu_drain(void *unused,
				      struct page *page, 
				      unsigned int order, 
				      int migratetype)
{
	
}

static void mtrace_mm_page_alloc_extfrag(void *unused,
					 struct page *page,
					 int alloc_order, 
					 int fallback_order,
					 int alloc_migratetype, 
					 int fallback_migratetype)
{

}

static void __mtrace_stack_state(struct mtrace_call_stack *stack, 
				 struct task_struct *task,
				 mtrace_call_state_t state)
{
	unsigned long call_enable;
	unsigned long tid = 0;
	int i = stack->curr;

	if (task)
		tid = task_pid_nr(task);
	mtrace_fcall_register(tid, stack->stack[i].pc, stack->stack[i].tag, 
			      i, state);
	call_enable = (state == mtrace_start || state == mtrace_resume);
	mtrace_call_set(call_enable, smp_processor_id());
}

static void __mtrace_push_call(struct mtrace_call_stack *stack, 
			       struct task_struct *task, unsigned long pc)
{
	atomic64_t *counter;
	unsigned long flags;
	int cpu;
	u64 tag;
	int i;

	local_irq_save(flags);
	
	cpu = smp_processor_id();

	if (stack->curr + 1 == MTRACE_CALL_STACK_DEPTH)
		panic("__mtrace_push_call: max depth exceeded");

	i = ++stack->curr;
	counter = &per_cpu(mtrace_call_tag, cpu);
	tag = atomic64_add_return(1, counter);

	if (tag & MTRACE_TAG_MASK)
		panic("__mtrace_push_call: out of tags");
	tag |= (u64)cpu << MTRACE_TAG_SHIFT;

	stack->stack[i].pc = pc;
	stack->stack[i].tag = tag;
	__mtrace_stack_state(stack, task, mtrace_start);

	local_irq_restore(flags);
}

static void __mtrace_pop_call(struct mtrace_call_stack *stack)
{
	int i;

	BUG_ON(stack->curr <= -1);

	/* NB in most cases the following must execute atomically. */
	__mtrace_stack_state(stack, NULL, mtrace_done);
	i = stack->curr--;
	stack->stack[i].pc = 0;
	stack->stack[i].tag = 0;
}

/*
 * XXX mt2db doesn't support call stacks yet.  
 * mtrace_{start,end}_do_page_fault is a hack to ensure the mtrace call
 * stack never has more than one item.
 */
void mtrace_start_do_page_fault(unsigned long pc)
{
	if (!current)
		return;
	if (current->mtrace_stack.curr != -1)
		return;

	__mtrace_push_call(&current->mtrace_stack, current, pc);
}

void mtrace_end_do_page_fault(void)
{
	unsigned long flags;
	int i;

	if (!current)
		return;

	local_irq_save(flags);
	i = current->mtrace_stack.curr;
	if (current->mtrace_stack.stack[i].pc == (unsigned long)do_page_fault)
		__mtrace_pop_call(&current->mtrace_stack);
	local_irq_restore(flags);
}

void mtrace_start_do_irq(unsigned long pc)
{
	struct mtrace_call_stack *stack;
	unsigned long flags;

	local_irq_save(flags);

	stack = &per_cpu(mtrace_irq_call_stack, smp_processor_id());

	if (stack->curr > -1)
		__mtrace_stack_state(stack, NULL, mtrace_pause);
	else if (current && current->mtrace_stack.curr > -1)
		__mtrace_stack_state(&current->mtrace_stack, NULL, mtrace_pause);

	__mtrace_push_call(stack, NULL, pc);
	local_irq_restore(flags);
}

void mtrace_end_do_irq(void)
{
	struct mtrace_call_stack *stack;
	unsigned long flags;

	local_irq_save(flags);

	stack = &per_cpu(mtrace_irq_call_stack, smp_processor_id());	
	__mtrace_pop_call(stack);

	if (stack->curr > -1)
		__mtrace_stack_state(stack, NULL, mtrace_resume);
	else if (current && current->mtrace_stack.curr > -1)
		__mtrace_stack_state(&current->mtrace_stack, current, mtrace_resume);
	local_irq_restore(flags);
}

void mtrace_start_entry(unsigned long pc)
{
	unsigned long flags;
	if (!current)
		return;

	local_irq_save(flags);
	__mtrace_push_call(&current->mtrace_stack, current, pc);
	local_irq_restore(flags);
}

void mtrace_end_entry(void)
{
	unsigned long flags;
	if (!current)
		return;

	local_irq_save(flags);
	__mtrace_pop_call(&current->mtrace_stack);
	local_irq_restore(flags);
}

static int mtrace_task_cmdline(struct task_struct *task, char *buffer, int n)
{
	strncpy(buffer, task->comm, n);
	return 0;
}

static void __mtrace_register_task(struct task_struct *tsk, mtrace_task_t type)
{
	char buffer[32];
	buffer[0] = 0;

	mtrace_task_cmdline(tsk, buffer, sizeof(buffer));
	mtrace_task_register(task_pid_nr(tsk), task_tgid_nr(tsk), type, buffer);
}

void mtrace_update_task(struct task_struct *tsk)
{
	__mtrace_register_task(tsk, mtrace_task_update);
}

void mtrace_init_task(struct task_struct *tsk)
{
	tsk->mtrace_stack.curr = -1;
	__mtrace_register_task(tsk, mtrace_task_init);
}

static void mtrace_exit_task(struct task_struct *t)
{
        while (t->mtrace_stack.curr >= 0)
		__mtrace_pop_call(&t->mtrace_stack);
	__mtrace_register_task(t, mtrace_task_exit);
}

static void mtrace_sched_switch(void *unused, struct task_struct *prev, 
				struct task_struct *next)
{
	unsigned long flags;

	local_irq_save(flags);

	if (prev->state == TASK_DEAD)
		mtrace_exit_task(prev);
	else
	    if (prev->mtrace_stack.curr >= 0)
		__mtrace_stack_state(&prev->mtrace_stack, NULL, mtrace_pause);

	mtrace_sched_record(task_pid_nr(next));

	if (next->mtrace_stack.curr >= 0)
		__mtrace_stack_state(&next->mtrace_stack, next, mtrace_resume);
	local_irq_restore(flags);
}

#ifdef CONFIG_LOCKDEP
void mtrace_lock_acquire(struct lockdep_map *lock, unsigned long ip,
                         int read)
{
	unsigned long flags;

	if (unlikely(current->lockdep_recursion))
                return;

	local_irq_save(flags);
        mtrace_disable_count();
        
	mtrace_lock_register(ip, lock, lock->name, mtrace_lockop_acquire, read);

        mtrace_enable_count();
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(mtrace_lock_acquire);

void mtrace_lock_release(struct lockdep_map *lock,
                         unsigned long ip)
{
	unsigned long flags;

	if (unlikely(current->lockdep_recursion))
                return;

	local_irq_save(flags);
        mtrace_disable_count();

	mtrace_lock_register(ip, lock, lock->name, mtrace_lockop_release, 0);

        mtrace_enable_count();
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(mtrace_lock_release);

void mtrace_lock_acquired(struct lockdep_map *lock, unsigned long ip)
{
	unsigned long flags;

	if (unlikely(current->lockdep_recursion))
                return;

	local_irq_save(flags);
        mtrace_disable_count();

	mtrace_lock_register(ip, lock, lock->name, mtrace_lockop_acquired, 0);	

        mtrace_enable_count();
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(mtrace_lock_acquired);
#endif

static inline int mtrace_atomic_add_unless(atomic_t *v, int a, int u, 
					   struct lockdep_map *dep_map)
{
	int c, old;

	/* The first try should be part of the 'work' calculation */
	spin_acquire(dep_map, 0, 0, _RET_IP_);
	c = atomic_read(v);
	if (unlikely(c == (u)))
		goto done;
	old = atomic_cmpxchg((v), c, c + (a));
	if (likely(old == c))
		goto done;
	c = old;
	spin_release(dep_map, 1, _RET_IP_);	

	/* Subsequent tries should be deducted from the 'work' */
	spin_acquire(dep_map, 0, 0, _RET_IP_);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	lock_acquired(dep_map, _RET_IP_);

done:
	spin_release(dep_map, 1, _RET_IP_);
	return c != (u);
}

int mtrace_atomic_dec_and_lock(atomic_t *atomic, 
			       struct lockdep_map *dep_map, 
			       spinlock_t *lock)
{
	/* Subtract 1 from counter unless that drops it to 0 (ie. it was 1) */
	if (mtrace_atomic_add_unless(atomic, -1, 1, dep_map))
		return 0;

	/* Otherwise do it the slow way */
	spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock(lock);
	return 0;
}

void __init mtrace_init_missing(void)
{
	pg_data_t *pgdat;
	for_each_online_pgdat(pgdat)
		mtrace_label_register(mtrace_label_heap, pgdat, sizeof(*pgdat),
				      "pglist_data", strlen("pglist_data"), 0);

	mtrace_label_register(mtrace_label_static, 
			      &vsyscall_gtod_data, sizeof(vsyscall_gtod_data),
			      "vsyscall_gtod_data", strlen("vsyscall_gtod_data"), 0);
}

void __init mtrace_init(void)
{
#define REG(name) BUG_ON(register_trace_##name(mtrace_##name, NULL))
	int ret;

	ret = register_trace_kmalloc(mtrace_kmem_alloc, NULL);
	BUG_ON(ret);
	ret = register_trace_kmem_cache_alloc(mtrace_kmem_alloc, NULL);
	BUG_ON(ret);

	ret = register_trace_kmalloc_node(mtrace_kmem_alloc_node, NULL);
	BUG_ON(ret);
	ret = register_trace_kmem_cache_alloc_node(mtrace_kmem_alloc_node, NULL);
	BUG_ON(ret);

	ret = register_trace_kfree(mtrace_kmem_free, NULL);
	BUG_ON(ret);
	ret = register_trace_kmem_cache_free(mtrace_kmem_free, NULL);
	BUG_ON(ret);

	REG(mm_page_free_direct);
	REG(mm_pagevec_free);
	REG(mm_page_alloc);
	REG(mm_page_alloc_zone_locked);
	REG(mm_page_pcpu_drain);
	REG(mm_page_alloc_extfrag);

	if (current)
		mtrace_sched_record(task_pid_nr(current));
	REG(sched_switch);

#ifdef CONFIG_LOCKDEP
	debug_locks = 0;
#endif

	mtrace_init_missing();

#undef REG
}
