extern void syscount_add(unsigned long call, struct timespec start, struct timespec stop);
extern void syscount_end_task(struct task_struct *tsk);

#define SYSCOUNT_DUP_MMAP_REST 507
#define SYSCOUNT_DUP_MMAP_KMEM 508
#define SYSCOUNT_DUP_MMAP_FILE 509 
#define SYSCOUNT_DUP_MMAP_COPY 510 
#define SYSCOUNT_DUP_MMAP_LOOP 511 
#define SYSCOUNT_MAX_CALLS     512
