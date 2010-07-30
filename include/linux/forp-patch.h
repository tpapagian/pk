#ifndef _LINUX_FORP_PATCH_H
#define _LINUX_FORP_PATCH_H

#ifndef __ASSEMBLY__

struct forp_rec {
	u64 time;
	u64 count;
};

struct forp_label {
	char name[32];
	int depth;
};

extern void forp_start(unsigned int id);
extern void forp_end(void);

static inline u64 forp_time(void)
{
	return __native_read_tsc();
}

#endif /* __ASSEMBLY__ */

#define FORP_ENTRY_PGFAULT NR_syscalls

#define FORP_ENABLE_INST   0x01
#define FORP_ENABLE_ENTRY  0x02

#endif
