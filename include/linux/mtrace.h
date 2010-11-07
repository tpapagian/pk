extern void mtrace_init(void);
extern void mtrace_init_task(struct task_struct *t);
extern void mtrace_exit_task(struct task_struct *t);

#define INIT_MTRACE .mtrace_curr_stack = -1,
#define MTRACE_CALL_STACK_DEPTH 50
