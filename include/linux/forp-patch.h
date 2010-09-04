#ifndef _LINUX_FORP_PATCH_H
#define _LINUX_FORP_PATCH_H

#ifndef __ASSEMBLY__

struct forp_rec {
	u64 time;
	u64 count;
	u64 sched;
};

struct forp_label {
	char name[32];
	int depth;
};

extern unsigned long __forp_push(unsigned int id);
extern void __forp_pop(void);

#define forp_push(id, flags)				\
	do {					       	\
		typecheck(unsigned long, flags);	\
		flags = __forp_push(id); 		\
	} while (0)

#define forp_pop(flags)					\
      do {					        \
		typecheck(unsigned long, flags);	\
		if (flags)	   	 		\
			__forp_pop();			\
      } while(0)

static inline u64 forp_time(void)
{
	return __native_read_tsc();
}

enum {
	FORP_STATIC_SCHEDULE = 0,
};

#endif /* __ASSEMBLY__ */

#include <asm/unistd.h>

#define FORP_ENTRY_BASE		NR_syscalls
#define FORP_ENTRY_PGFAULT	(FORP_ENTRY_BASE + 0)
#define FORP_ENTRY_SOFTIRQD	(FORP_ENTRY_BASE + 1)
#define FORP_ENTRY_IDLE		(FORP_ENTRY_BASE + 2)

#define FORP_ENABLE_INST   	0x01
#define FORP_ENABLE_ENTRY  	0x02
#define FORP_ENABLE_ALL		(FORP_ENABLE_INST|FORP_ENABLE_ENTRY)

#endif
