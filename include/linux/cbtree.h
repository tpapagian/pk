/*
 *  Concurrent balanced trees
 */

#ifndef	_LINUX_CBTREE_H
#define	_LINUX_CBTREE_H

#include <linux/kernel.h>
#include <linux/stddef.h>

struct cb_root
{
	// XXX Should be SHARED(...)
        struct TreeBB_Node *root;
};

#define CB_ROOT	(struct cb_root) { NULL, }

#define CB_EMPTY_ROOT(cbroot)	(GET((cbroot)->root) == NULL)

#endif	/* _LINUX_CBTREE_H */
