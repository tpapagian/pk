/*
 *  Concurrent balanced trees
 */

#ifndef	_LINUX_CBTREE_H
#define	_LINUX_CBTREE_H

#include <linux/kernel.h>
#include <linux/stddef.h>

// Sequential operations
#define SHARED(typ, name) struct { typ val; } name
#define ATOMIC_FETCH_AND_INC(sh) ((sh).val++)
#define SET(sh, v) ((sh).val = (v))
#define GET(sh) ((const __typeof__((sh).val)) (sh).val)

// Helpers
#define INC(sh) \
        ({__typeof__(sh) *__ptr = &(sh); SET(*__ptr, GET(*__ptr)+1)})

struct cb_root
{
        SHARED(struct TreeBB_Node *, root);
};

#define CB_ROOT	(struct cb_root) { NULL, }

#define CB_EMPTY_ROOT(cbroot)	(GET((cbroot)->root) == NULL)

#endif	/* _LINUX_CBTREE_H */
