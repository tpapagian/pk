#ifndef _LINUX_FORP_H_
#define _LINUX_FORP_H_

#ifdef CONFIG_FORP

#define FORP_RETSTACK_DEPTH 50

#define INIT_FORP		.forp_curr_stack = -1,

struct forp_call_stamp {
	unsigned int id;
	unsigned long long calltime;
	unsigned long long sched;
};

extern void forp_init_task(struct task_struct *t);
extern void forp_exit_task(struct task_struct *t);
extern void forp_sched_switch(struct task_struct *prev, struct task_struct *next);
extern void forp_start_entry(unsigned long entry);
extern void forp_end_entry(void);
extern void forp_stamp_static(unsigned long static_id, struct forp_call_stamp *f);
extern void forp_add_stamp(struct forp_call_stamp *f);
#else /* !CONFIG_FORP */
#define INIT_FORP
static inline void forp_init_task(struct task_struct *t) { }
static inline void forp_exit_task(struct task_struct *t) { }
static inline void forp_sched_switch(struct task_struct *prev, struct task_struct *next) {}
static inline void forp_start_entry(unsigned long entry) { }
static inline void forp_end_entry(void) { }
static inline void forp_stamp_static(unsigned long static_id, 
				     struct forp_call_stamp *f) { }
static inline void forp_add_stamp(struct forp_call_stamp *f) { }
#endif /* CONFIG_FORP */
#endif /* _LINUX_FORP_H_ */
