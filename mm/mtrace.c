#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tracepoint.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <trace/events/kmem.h>
#include <trace/events/syscalls.h>
#include <trace/events/sched.h>

#include <asm/mtrace-magic.h>

static DEFINE_PER_CPU_ALIGNED(atomic64_t, mtrace_call_tag);

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

static void __mtrace_fcall_start(struct task_struct *tsk)
{
	int i = tsk->mtrace_curr_stack;
	mtrace_fcall_register(task_pid_nr(tsk), tsk->mtrace_call_stack[i].pc, 
			      tsk->mtrace_call_stack[i].tag, i, 0);
}

static void __mtrace_fcall_stop(struct task_struct *tsk)
{
	int i = tsk->mtrace_curr_stack;
	mtrace_fcall_register(task_pid_nr(tsk), tsk->mtrace_call_stack[i].pc, 
			      tsk->mtrace_call_stack[i].tag, i, 1);
}

static void __mtrace_push_call(struct task_struct *tsk, unsigned long pc)
{
	atomic64_t *counter;
	u64 tag;
	int i;
	
	i = ++tsk->mtrace_curr_stack;
	counter = &per_cpu(mtrace_call_tag, smp_processor_id());
	tag = atomic64_add_return(1, counter);

	tsk->mtrace_call_stack[i].pc = pc;
	tsk->mtrace_call_stack[i].tag = tag;
	__mtrace_fcall_start(tsk);
}

void mtrace_start_entry(long id)
{
	if (!current)
		return;
	__mtrace_push_call(current, sys_call_table[id]);
}

static void __mtrace_pop_call(struct task_struct *tsk)
{
	int i;

	BUG_ON(tsk->mtrace_curr_stack <= -1);
	__mtrace_fcall_stop(tsk);
	i = tsk->mtrace_curr_stack--;
	tsk->mtrace_call_stack[i].pc = 0;
	tsk->mtrace_call_stack[i].tag = 0;
}

void mtrace_end_entry(void)
{
	if (!current)
		return;
	__mtrace_pop_call(current);
}

void mtrace_init_task(struct task_struct *tsk)
{
	tsk->mtrace_curr_stack = -1;
}

void mtrace_exit_task(struct task_struct *t)
{
        if (t == NULL)
		return;
	
        while (t->mtrace_curr_stack >= 0)
		__mtrace_pop_call(t);
}

static void mtrace_sched_switch(void *unused, struct task_struct *prev, 
				struct task_struct *next)
{
	if (prev->mtrace_curr_stack >= 0)
		__mtrace_fcall_stop(prev);

	if (next->mtrace_curr_stack >= 0)
		__mtrace_fcall_start(next);
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

	REG(sched_switch);

#undef REG
}
