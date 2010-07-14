#ifndef _LINUX_FORP_PATCH_H
#define _LINUX_FORP_PATCH_H

struct forp_rec {
	const char *name;
	int depth;

	/* private */
	int id;
	u64 time;
	u64 count;
};

extern void forp_start(unsigned int id);
extern void forp_end(void);

static inline u64 forp_time(void)
{
	return __native_read_tsc();
}

#endif
