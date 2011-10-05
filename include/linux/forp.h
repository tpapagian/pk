#ifndef _LINUX_FORP_H_
#define _LINUX_FORP_H_

struct forp_call_stamp {
	unsigned int id;
	unsigned long long calltime;
	unsigned long long sched;
	unsigned long long irq;
};

#ifdef CONFIG_FORP

#define FORP_RETSTACK_DEPTH 50

#define INIT_FORP		.forp_call_stack.depth = -1, \
				.forp_irq_stack.depth = -1,

struct forp_stack {
	int depth;
	struct forp_call_stamp forp_stack[FORP_RETSTACK_DEPTH];

	int forp_entry_start;
	struct forp_call_stamp forp_entry;

	u64 forp_switchstamp;
};

extern void forp_init_task(struct task_struct *t);
extern void forp_exit_task(struct task_struct *t);
extern void forp_sched_switch(struct task_struct *prev, struct task_struct *next);
extern void forp_irq_enter(int irq);
extern void forp_irq_exit(void);
extern void forp_start_entry(unsigned long entry);
extern void forp_end_entry(void);
extern void forp_stamp_static(unsigned long static_id, struct forp_call_stamp *f);
extern void forp_add_stamp(struct forp_call_stamp *f);
extern int forp_get_context(void);
extern struct forp_label *forp_get_context_label(int i);
#else /* !CONFIG_FORP */
#define INIT_FORP
static inline void forp_init_task(struct task_struct *t) { }
static inline void forp_exit_task(struct task_struct *t) { }
static inline void forp_sched_switch(struct task_struct *prev, struct task_struct *next) {}
static inline void forp_irq_enter(int irq) {}
static inline void forp_irq_exit(void) {}
static inline void forp_start_entry(unsigned long entry) { }
static inline void forp_end_entry(void) { }
static inline void forp_stamp_static(unsigned long static_id, 
				     struct forp_call_stamp *f) { }
static inline void forp_add_stamp(struct forp_call_stamp *f) { }
static inline int forp_get_context(void) { return -1; }
static inline struct forp_label *forp_get_context_label(int i) {return NULL; }
#endif /* CONFIG_FORP */
#endif /* _LINUX_FORP_H_ */
