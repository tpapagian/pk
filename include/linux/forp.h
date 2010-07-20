#ifndef _LINUX_FORP_H_
#define _LINUX_FORP_H_

#ifdef CONFIG_FORP

#define FORP_RETSTACK_DEPTH 50

#define INIT_FORP		.forp_curr_stack = -1,

struct forp_ret_stack {
	unsigned int id;
	unsigned long long calltime;
	unsigned long long subtime;
};

extern void forp_init_task(struct task_struct *t);
extern void forp_exit_task(struct task_struct *t);
#else /* !CONFIG_FORP */
#define INIT_FORP
static inline void forp_init_task(struct task_struct *t) { }
static inline void forp_exit_task(struct task_struct *t) { }
#endif /* CONFIG_FORP */
#endif /* _LINUX_FORP_H_ */
