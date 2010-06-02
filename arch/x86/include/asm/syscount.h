void syscount_add(unsigned long call, struct timespec start, struct timespec stop);
extern void syscount_end_task(struct task_struct *tsk);

#define SYSCOUNT_MNT_PUT1      500
#define SYSCOUNT_MNT_PUT2      501
#define SYSCOUNT_FPUT          502
#define SYSCOUNT_IXGBE_RX      503
#define SYSCOUNT_IXGBE_TX      504
#define SYSCOUNT_DUP_MMAP_ANON 505
#define SYSCOUNT_DUP_MMAP_MPOL 506
#define SYSCOUNT_DUP_MMAP_STAR 507
#define SYSCOUNT_DUP_MMAP_KMEM 508
#define SYSCOUNT_DUP_MMAP_FILE 509 
#define SYSCOUNT_DUP_MMAP_COPY 510 
#define SYSCOUNT_DUP_MMAP_LOOP 511 
#define SYSCOUNT_MAX_CALLS     512
