//#include <assert.h>
//#include <stdbool.h>
//#include <stdlib.h>
//#include <stdio.h>
//#include <limits.h>

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cbtree.h>
#include <linux/module.h>

//#include "mcorelib/mcorelib.h"

// XXX
#define RCU 0
#if RCU
void rcu_init(void);
void rcu_gc(void);
void rcu_delayed(void *e, void (*dofree)(void *));
void rcu_begin_read(int tid);
void rcu_end_read(int tid);
void rcu_begin_write(int tid);
void rcu_end_write(int tid);
#endif

/******************************************************************
 * Delayed free list
 */

enum { MAX_DFREE = 100 };
void *dfreeList[MAX_DFREE];
int dfreePos;
int totalFreed;

static inline void
dfree(void *ptr)
{
        int i;

        totalFreed++;
#if RCU
        rcu_delayed(ptr, free);
#else
        assert(dfreePos < MAX_DFREE);
        dfreeList[dfreePos++] = ptr;

        for (i = 0; i < dfreePos-1; ++i)
                assert(dfreeList[i] != ptr);
#endif
}

void
dfreeReleaseAll(void)
{
#if !RCU
        if (dfreePos)
                printf("Freeing %d\n", dfreePos);
        for (int i = 0; i < dfreePos; ++i)
                free(dfreeList[i]);
        dfreePos = 0;
#endif
}

/******************************************************************
 * Tree
 */

enum { INPLACE = 1 };

struct TreeBB_Node
{
        SHARED(struct TreeBB_Node *, left);
        SHARED(struct TreeBB_Node *, right);
        SHARED(unsigned int, size);

        int value;
};

typedef struct TreeBB_Node node_t;

static struct kmem_cache *node_cache;

static int __init
TreeBB_Init(void)
{
        node_cache = kmem_cache_create("struct TreeBB_Node",
                                       sizeof(node_t), 0, SLAB_PANIC, NULL);
        return 0;
}

core_initcall(TreeBB_Init);

enum { WEIGHT = 4 };

static inline int
nodeSize(node_t *node)
{
        return node ? GET(node->size) : 0;
}

static inline node_t *
mkNode(node_t *left, node_t *right, int value)
{
//        node_t *node = malloc(sizeof *node);
        node_t *node = kmem_cache_alloc(node_cache, GFP_ATOMIC);
        SET(node->left, left);
        SET(node->right, right);
        SET(node->size, 1 + nodeSize(left) + nodeSize(right));
        node->value = value;
        return node;
}

static node_t *
singleL(node_t *left, node_t *right, int value)
{
//        printf("%s\n", __func__);
        dfree(right);
        return mkNode(mkNode(left, GET(right->left), value),
                      GET(right->right), right->value);
}

static node_t *
doubleL(node_t *left, node_t *right, int value)
{
//        printf("%s\n", __func__);
        dfree(right);
        dfree(GET(right->left));
        return mkNode(mkNode(left, GET(GET(right->left)->left), value),
                      mkNode(GET(GET(right->left)->right), GET(right->right),
                             right->value),
                      GET(right->left)->value);
}

static node_t *
singleR(node_t *left, node_t *right, int value)
{
//        printf("%s\n", __func__);
        dfree(left);
        return mkNode(GET(left->left), mkNode(GET(left->right), right, value),
                      left->value);
}

static node_t *
doubleR(node_t *left, node_t *right, int value)
{
//        printf("%s\n", __func__);
        dfree(left);
        dfree(GET(left->right));
        return mkNode(mkNode(GET(left->left), GET(GET(left->right)->left),
                             left->value),
                      mkNode(GET(GET(left->right)->right), right, value),
                      GET(left->right)->value);
}

static node_t *
mkBalancedL(node_t *left, node_t *right, int value)
{
        int rln = nodeSize(GET(right->left)),
                rrn = nodeSize(GET(right->right));
        if (rln < rrn)
                return singleL(left, right, value);
        return doubleL(left, right, value);
}

static node_t *
mkBalancedR(node_t *left, node_t *right, int value)
{
        int lln = nodeSize(GET(left->left)),
                lrn = nodeSize(GET(left->right));
        if (lrn < lln)
                return singleR(left, right, value);
        return doubleR(left, right, value);
}

/**
 * Create a balanced node from the given children to replace cur.  In
 * non-destructive mode, this always returns a new node.  In
 * in-place-mode, it may update cur directly if no rotations are
 * necessary; otherwise it returns a new node, which the caller must
 * arrange to swap in to the tree in place of cur.  replace should be
 * 0 if the left subtree of cur is being replaced, or 1 if the right
 * subtree of cur is being replaced.  inPlace specifies whether cur
 * can be modified in place.  This will always free any nodes that are
 * discarded (including cur if it gets replaced).
 *
 * This is written to be lightweight enough to get inlined if
 * 'replace' and 'inPlace' are compile-time constants.  Once inlined,
 * constant folding will eliminate most of the code.
 */
static inline node_t *
mkBalanced(node_t *cur, node_t *left, node_t *right, int replace, bool inPlace)
{
        int ln = nodeSize(left), rn = nodeSize(right);
        int value = cur->value;

        if (!inPlace)
                dfree(cur);

        if (ln+rn < 2)
                goto balanced;
        if (rn > WEIGHT * ln) {
                if (inPlace)
                        dfree(cur);
                return mkBalancedL(left, right, value);
        }
        if (ln > WEIGHT * rn) {
                if (inPlace)
                        dfree(cur);
                return mkBalancedR(left, right, value);
        }

balanced:
        if (inPlace) {
                // When updating in-place, we only ever modify one of
                // the two pointers as our visible write.  We also
                // modify the size, but this is okay because size is
                // never in a reader's read set.
                if (replace == 0) {
                        assert(GET(cur->right) == right);
                        SET(cur->left, left);
                } else {
                        assert(GET(cur->left) == left);
                        SET(cur->right, right);
                }
                SET(cur->size, 1 + nodeSize(left) + nodeSize(right));
                return cur;
        } else {
                return mkNode(left, right, value);
        }
}

static node_t *
insert(node_t *node, int value)
{
        if (!node)
                return mkNode(NULL, NULL, value);

        // Note that, even in in-place mode, we rebalance the tree
        // from the bottom up.  This has some nifty properties beyond
        // code simplicity.  In particular, the size fields get
        // updated from the bottom up, after we've performed the
        // insert or discovered it was unnecessary, which means we
        // don't need a second pass to decrement them if the element
        // turns out to be in the tree already.  However, this may be
        // incompatible with lazy write locking.  At the least, the
        // delayed updates to the size fields combined with competing
        // writers might result in tree imbalance.

        if (value < node->value)
                return mkBalanced(node, insert(GET(node->left), value),
                                  GET(node->right), 0, INPLACE);
        if (value > node->value)
                return mkBalanced(node, GET(node->left),
                                  insert(GET(node->right), value),
                                  1, INPLACE);
        return node;
}

void
TreeBB_Insert(struct TreeBB *tree, int value)
{
#if RCU
        rcu_begin_write(getCPU());
#endif
        SET(tree->root, insert(GET(tree->root), value));
#if RCU
        rcu_end_write(getCPU());
        rcu_gc();
#else
        dfreeReleaseAll();
#endif
}

static node_t *
deleteMin(node_t *node, node_t **minOut)
{
        node_t *left = GET(node->left), *right = GET(node->right);
        if (!left) {
                *minOut = node;
                return right;
        }
        // This must always happen non-destructively because the goal
        // is to move the min element up in the tree, so the in-place
        // write must be performed up there to make this operation
        // atomic.  (Alternatively, a concurrent reader may already be
        // walking between the element being replaced and the min
        // element being removed, which means we must keep this min
        // element visible.)
        return mkBalanced(node, deleteMin(left, minOut), right, 0, false);
}

static node_t *
delete(node_t *node, int value)
{
        // XXX Will crash if value isn't in the tree
        node_t *left = GET(node->left), *right = GET(node->right);
        if (value < node->value)
                return mkBalanced(node, delete(left, value), right, 0, INPLACE);
        if (value > node->value)
                return mkBalanced(node, left, delete(right, value), 1, INPLACE);

        // We found our node to delete
        dfree(node);
        if (!left)
                return right;
        if (!right)
                return left;
        node_t *min;
        right = deleteMin(right, &min);
        // This needs to be performed non-destructively because the
        // min element is still linked in to the tree below us.  Thus,
        // we need to create a new min element here, which will be
        // atomically swapped in to the tree by our parent (along with
        // the new subtree where the min element has been removed).f
        return mkBalanced(min, left, right, 1, false);
}

void
TreeBB_Delete(struct TreeBB *tree, int value)
{
#if RCU
        rcu_begin_write(getCPU());
#endif
        SET(tree->root, delete(GET(tree->root), value));
#if RCU
        rcu_end_write(getCPU());
#else
        dfreeReleaseAll();
#endif
}

bool
TreeBB_Contains(struct TreeBB *tree, int value)
{
#if RCU
        rcu_begin_read(getCPU());
#endif
        node_t *node = GET(tree->root);
        while (node) {
                if (node->value == value)
                        break;
                if (node->value < value)
                        node = GET(node->left);
                else
                        node = GET(node->right);
        }
#if RCU
        rcu_end_read(getCPU());
#endif
        return !!node;
}

/******************************************************************
 * Tree debugging
 */

static void
check(node_t *node, int min, int max, const int *vals, int *pos)
{
        if (!node)
                return;

        assert(node->value > min);
        assert(node->value < max);
        assert(nodeSize(node) ==
               1 + nodeSize(GET(node->left)) + nodeSize(GET(node->right)));

        check(GET(node->left), min, node->value, vals, pos);
        assert(node->value == vals[*pos]);
        (*pos)++;
        check(GET(node->right), node->value, max, vals, pos);
}

static void
show(node_t *node, int depth)
{
        if (!node)
                return;

        show(GET(node->left), depth + 1);
        printk("%*s%d\n", depth*2, "", node->value);
        show(GET(node->right), depth + 1);
}

void
TreeBB_Check(struct TreeBB *tree, const int *vals)
{
        int pos = 0;
        check(GET(tree->root), INT_MIN, INT_MAX, vals, &pos);
}

#if 0
static int
cmpInt(const void *p1, const void *p2)
{
        return (*(const int*)p1) - (*(const int*)p2);
}

int
main(int argc, char **argv)
{
        enum { LEN = 10000, DEL = 50 };
        struct TreeBB tree = {};
        int vals[LEN];

#if RCU
        rcu_init();
#endif

        for (int i = 0; i < LEN; ++i) {
                int val = rand();
                printf("+++ %d\n", val);
                TreeBB_Insert(&tree, val);
                vals[i] = val;
        }
        int insertFrees = totalFreed;
        totalFreed = 0;

        for (int i = 0; i < DEL; ++i) {
                printf("--- %d\n", vals[i]);
                TreeBB_Delete(&tree, vals[i]);
        }
        int deleteFrees = totalFreed;
        totalFreed = 0;

        qsort(vals+DEL, LEN-DEL, sizeof vals[0], cmpInt);
        assert(nodeSize(GET(tree.root)) == LEN-DEL);
        TreeBB_Check(&tree, vals+DEL);
        printf("%d freed by %d inserts, %d freed by %d deletes\n",
               insertFrees, LEN, deleteFrees, DEL);
}
#endif
