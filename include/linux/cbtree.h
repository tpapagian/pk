/*
 *  Concurrent balanced trees
 */

#ifndef	_LINUX_CBTREE_H
#define	_LINUX_CBTREE_H

#include <linux/kernel.h>
#include <linux/stddef.h>

struct cb_node
{
  unsigned long key;
  struct cb_node *r;
  struct cb_node *l;
  unsigned long  size;
} __attribute__((aligned(sizeof(long))));
    /* The alignment might seem pointless, but allegedly CRIS needs it */

struct cb_root
{
	struct cb_node *cb_node;
};

#define CB_ROOT	(struct cb_root) { NULL, }
//#define	cb_entry(ptr, type, member) container_of(ptr, type, member)

#define CB_EMPTY_ROOT(root)	((root)->cb_node == NULL)

/* Find logical next and previous nodes in a tree */
extern struct cb_node *cbnext(const struct cb_node *);
extern struct cb_node *cbprev(const struct cb_node *);
extern struct cb_node *cbfirst(const struct cb_root *);
extern struct cb_node *cblast(const struct cb_root *);

#endif	/* _LINUX_CBTREE_H */
