#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tracepoint.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <trace/events/kmem.h>
#include <trace/events/syscalls.h>
#include <trace/events/sched.h>
#include <trace/events/lock.h>

#include <linux/kprobes.h>
#include <linux/ptrace.h>

#include <asm/traps.h>

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
				 mtrace_call_state_t state)
{
	int i = stack->curr;
	mtrace_fcall_register(0, stack->stack[i].pc, 
			      stack->stack[i].tag, i, state);
}

static void __mtrace_push_call(struct mtrace_call_stack *stack, unsigned long pc)
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
	__mtrace_stack_state(stack, mtrace_start);

	local_irq_restore(flags);
}

static void __mtrace_pop_call(struct mtrace_call_stack *stack)
{
	int i;

	BUG_ON(stack->curr <= -1);
	__mtrace_stack_state(stack, mtrace_done);
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

	__mtrace_push_call(&current->mtrace_stack, pc);
}

void mtrace_end_do_page_fault(void)
{
	int i;

	if (!current)
		return;
	
	i = current->mtrace_stack.curr;
	if (current->mtrace_stack.stack[i].pc == (unsigned long)do_page_fault)
		__mtrace_pop_call(&current->mtrace_stack);
}

void mtrace_start_do_irq(unsigned long pc)
{
	struct mtrace_call_stack *stack;

	stack = &per_cpu(mtrace_irq_call_stack, smp_processor_id());

	if (stack->curr > -1)
		__mtrace_stack_state(stack, mtrace_pause);
	else if (current && current->mtrace_stack.curr > -1)
		__mtrace_stack_state(&current->mtrace_stack, mtrace_pause);

	__mtrace_push_call(stack, pc);
}

void mtrace_end_do_irq(void)
{
	struct mtrace_call_stack *stack;

	stack = &per_cpu(mtrace_irq_call_stack, smp_processor_id());	
	__mtrace_pop_call(stack);

	if (stack->curr > -1)
		__mtrace_stack_state(stack, mtrace_resume);
	else if (current && current->mtrace_stack.curr > -1)
		__mtrace_stack_state(&current->mtrace_stack, mtrace_resume);
}

void mtrace_start_entry(unsigned long pc)
{
	if (!current)
		return;
	__mtrace_push_call(&current->mtrace_stack, pc);
}

void mtrace_end_entry(void)
{
	if (!current)
		return;
	__mtrace_pop_call(&current->mtrace_stack);
}

void mtrace_init_task(struct task_struct *tsk)
{
	tsk->mtrace_stack.curr = -1;
}

void mtrace_exit_task(struct task_struct *t)
{
        if (t == NULL)
		return;
	
        while (t->mtrace_stack.curr >= 0)
		__mtrace_pop_call(&t->mtrace_stack);
}

static void mtrace_sched_switch(void *unused, struct task_struct *prev, 
				struct task_struct *next)
{
	if (prev->mtrace_stack.curr >= 0)
		__mtrace_stack_state(&prev->mtrace_stack, mtrace_pause);

	if (next->mtrace_stack.curr >= 0)
		__mtrace_stack_state(&next->mtrace_stack, mtrace_resume);
}

#ifdef CONFIG_LOCKDEP
static void mtrace_lock_acquire(void *unused, struct lockdep_map *lock,
                                unsigned int subclass, int trylock, int read,
                                int check, struct lockdep_map *next_lock,
                                unsigned long ip)
{
	mtrace_lock_register(ip, lock->name, 0, read);
	/* 
	 * static int i;
	 * if (++i % 1000 == 0)
	 * printk(KERN_INFO "lock_acquire(%s, %lu)\n",
	 *	  lock->name, ip);
	 */
}

static void mtrace_lock_release(void *unused, struct lockdep_map *lock,
				unsigned long ip)
{
	mtrace_lock_register(ip, lock->name, 1, 0);
}
#endif

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

	REG(sched_switch);

#ifdef CONFIG_LOCKDEP
	REG(lock_acquire);
	REG(lock_release);
#endif

#undef REG

	mtrace_enable_set(1, "all", 3);
}
