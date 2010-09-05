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

typedef u8 forp_flags_t;

extern forp_flags_t __forp_push(unsigned int id);
extern void __forp_pop(void);

/* forp_push and forp_pop usage:
 * 
 * void foo(void) 
 * {
 *	forp_flags_t forp0, forp1;
 * 	forp_push(0);
 * 	goo();
 * 	forp_push(1);
 * 	poo();
 * 	forp_pop(1);
 * 	forp_pop(0);
 * }
*/

#define forp_push(id)					\
	do {					       	\
		typecheck(forp_flags_t, forp##id);	\
		forp##id = __forp_push(id); 		\
	} while (0)

#define forp_pop(id)					\
      do {					        \
		typecheck(forp_flags_t, forp##id);	\
		if (forp##id)	   	 		\
			__forp_pop();			\
      } while(0)

enum {
	FORP_STATIC_SCHEDULE = 0,
};

#endif /* __ASSEMBLY__ */

#include <asm/unistd.h>

#define FORP_ENTRY_BASE		NR_syscalls
#define FORP_ENTRY_PGFAULT	(FORP_ENTRY_BASE + 0)
#define FORP_ENTRY_SOFTIRQD	(FORP_ENTRY_BASE + 1)
#define FORP_ENTRY_IDLE		(FORP_ENTRY_BASE + 2)

#define FORP_ENABLE_DYN   	0x01
#define FORP_ENABLE_ENTRY  	0x02
#define FORP_ENABLE_ALL		(FORP_ENABLE_DYN|FORP_ENABLE_ENTRY)

#endif
