extern void syscount_add(unsigned long call, struct timespec start, struct timespec stop);

#define SYSCOUNT_DUP_MMAP_COPY 510 
#define SYSCOUNT_DUP_MMAP_LOOP 511 
#define SYSCOUNT_MAX_CALLS     512
