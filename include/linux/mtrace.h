#ifndef _LINUX_MTRACE_H_
#define _LINUX_MTRACE_H_

#ifdef CONFIG_MTRACE
extern void mtrace_init(void);
extern void mtrace_init_task(struct task_struct *t);
extern void mtrace_update_task(struct task_struct *t);

extern void mtrace_end_do_irq(void);
extern void mtrace_start_do_irq(unsigned long pc);

extern void mtrace_start_entry(unsigned long pc);
extern void mtrace_end_entry(void);

#define INIT_MTRACE .mtrace_stack = { .curr = -1 }
#define MTRACE_CALL_STACK_DEPTH 50

struct mtrace_call {
	u64 tag;
	u64 pc;
};

struct mtrace_call_stack {
	int curr;
	struct mtrace_call stack[MTRACE_CALL_STACK_DEPTH];
};

extern int mtrace_atomic_dec_and_lock(atomic_t *atomic, 
				      struct lockdep_map *dep_map, 
				      spinlock_t *lock);
#else /* CONFIG_MTRACE */
static inline void mtrace_init(void) {}
static inline void mtrace_init_task(struct task_struct *t) {}
static inline void mtrace_update_task(struct task_struct *t) {}
static inline void mtrace_end_do_irq(void) {}
static inline void mtrace_start_do_irq(unsigned long pc) {}
static inline void mtrace_start_entry(unsigned long pc) {}
static inline void mtrace_end_entry(void) {}
#define INIT_MTRACE
#endif /* CONFIG_MTRACE */
#endif /* _LINUX_MTRACE_H_ */
