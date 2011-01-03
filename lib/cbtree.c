#define KERNEL 1
#if !KERNEL
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "mcorelib/mcorelib.h"
#else
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cbtree.h>

#define assert(s) do{if (!(s)) panic(#s);} while(0);
#endif

// XXX
#define RCU 1
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
#if KERNEL
        // XXX Not implemented
#elif RCU
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

typedef uintptr_t k_t;

typedef struct
{
        k_t key;
        void *value;
} kv_t;

struct TreeBB_Node
{
        SHARED(struct TreeBB_Node *, left);
        SHARED(struct TreeBB_Node *, right);
        SHARED(unsigned int, size);

        kv_t kv;
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
mkNode(node_t *left, node_t *right, kv_t *kv)
{
//        node_t *node = malloc(sizeof *node);
        node_t *node = kmem_cache_alloc(node_cache, GFP_ATOMIC);
        SET(node->left, left);
        SET(node->right, right);
        SET(node->size, 1 + nodeSize(left) + nodeSize(right));
        node->kv = *kv;
        return node;
}

static node_t *
singleL(node_t *left, node_t *right, kv_t *kv)
{
//        printf("%s\n", __func__);
        dfree(right);
        return mkNode(mkNode(left, GET(right->left), kv),
                      GET(right->right), &right->kv);
}

static node_t *
doubleL(node_t *left, node_t *right, kv_t *kv)
{
//        printf("%s\n", __func__);
        dfree(right);
        dfree(GET(right->left));
        return mkNode(mkNode(left, GET(GET(right->left)->left), kv),
                      mkNode(GET(GET(right->left)->right), GET(right->right),
                             &right->kv),
                      &GET(right->left)->kv);
}

static node_t *
singleR(node_t *left, node_t *right, kv_t *kv)
{
//        printf("%s\n", __func__);
        dfree(left);
        return mkNode(GET(left->left), mkNode(GET(left->right), right, kv),
                      &left->kv);
}

static node_t *
doubleR(node_t *left, node_t *right, kv_t *kv)
{
//        printf("%s\n", __func__);
        dfree(left);
        dfree(GET(left->right));
        return mkNode(mkNode(GET(left->left), GET(GET(left->right)->left),
                             &left->kv),
                      mkNode(GET(GET(left->right)->right), right, kv),
                      &GET(left->right)->kv);
}

static node_t *
mkBalancedL(node_t *left, node_t *right, kv_t *kv)
{
        int rln = nodeSize(GET(right->left)),
                rrn = nodeSize(GET(right->right));
        if (rln < rrn)
                return singleL(left, right, kv);
        return doubleL(left, right, kv);
}

static node_t *
mkBalancedR(node_t *left, node_t *right, kv_t *kv)
{
        int lln = nodeSize(GET(left->left)),
                lrn = nodeSize(GET(left->right));
        if (lrn < lln)
                return singleR(left, right, kv);
        return doubleR(left, right, kv);
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
        kv_t *kv = &cur->kv;

        if (!inPlace)
                dfree(cur);

        if (ln+rn < 2)
                goto balanced;
        if (rn > WEIGHT * ln) {
                if (inPlace)
                        dfree(cur);
                return mkBalancedL(left, right, kv);
        }
        if (ln > WEIGHT * rn) {
                if (inPlace)
                        dfree(cur);
                return mkBalancedR(left, right, kv);
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
                return mkNode(left, right, kv);
        }
}

static node_t *
insert(node_t *node, kv_t *kv)
{
        if (!node)
                return mkNode(NULL, NULL, kv);

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

        if (kv->key < node->kv.key)
                return mkBalanced(node, insert(GET(node->left), kv),
                                  GET(node->right), 0, INPLACE);
        if (kv->key > node->kv.key)
                return mkBalanced(node, GET(node->left),
                                  insert(GET(node->right), kv),
                                  1, INPLACE);
        return node;
}

void
TreeBB_Insert(struct cb_root *tree, uintptr_t key, void *value)
{
        kv_t kv = {key, value};
#if KERNEL
        // XXX Not implemented
#elif RCU
        rcu_begin_write(getCPU());
#endif
        SET(tree->root, insert(GET(tree->root), &kv));
#if KERNEL
        // XXX Not implemented
#elif RCU
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
delete(node_t *node, k_t key)
{
        node_t *min;

        // XXX Will crash if value isn't in the tree
        node_t *left = GET(node->left), *right = GET(node->right);
        if (key < node->kv.key)
                return mkBalanced(node, delete(left, key), right, 0, INPLACE);
        if (key > node->kv.key)
                return mkBalanced(node, left, delete(right, key), 1, INPLACE);

        // We found our node to delete
        dfree(node);
        if (!left)
                return right;
        if (!right)
                return left;
        right = deleteMin(right, &min);
        // This needs to be performed non-destructively because the
        // min element is still linked in to the tree below us.  Thus,
        // we need to create a new min element here, which will be
        // atomically swapped in to the tree by our parent (along with
        // the new subtree where the min element has been removed).f
        return mkBalanced(min, left, right, 1, false);
}

void
TreeBB_Delete(struct cb_root *tree, uintptr_t key)
{
#if KERNEL
        // XXX Not implemented
#elif RCU
        rcu_begin_write(getCPU());
#endif
        SET(tree->root, delete(GET(tree->root), key));
#if KERNEL
        // XXX Not implemented
#elif RCU
        rcu_end_write(getCPU());
#else
        dfreeReleaseAll();
#endif
}

// XXX Need a lookup
bool
TreeBB_Contains(struct cb_root *tree, uintptr_t key)
{
        node_t *node;
#if KERNEL
        // XXX Not implemented
#elif RCU
        rcu_begin_read(getCPU());
#endif
        node = GET(tree->root);
        while (node) {
                if (node->kv.key == key)
                        break;
                if (node->kv.key < key)
                        node = GET(node->left);
                else
                        node = GET(node->right);
        }
#if KERNEL
        // XXX Not implemented
#elif RCU
        rcu_end_read(getCPU());
#endif
        return !!node;
}

/******************************************************************
 * Tree debugging
 */

static void
check(node_t *node, k_t min, k_t max, const k_t *keys, int *pos)
{
        if (!node)
                return;

        assert(node->kv.key > min);
        assert(node->kv.key < max);
        assert(nodeSize(node) ==
               1 + nodeSize(GET(node->left)) + nodeSize(GET(node->right)));

        check(GET(node->left), min, node->kv.key, keys, pos);
        assert(node->kv.key == keys[*pos]);
        (*pos)++;
        check(GET(node->right), node->kv.key, max, keys, pos);
}

static void
show(node_t *node, int depth)
{
        if (!node)
                return;

        show(GET(node->left), depth + 1);
        printk("%*s%p -> %p\n", depth*2, "", (void*)node->kv.key, node->kv.value);
        show(GET(node->right), depth + 1);
}

static void
TreeBB_Check(struct cb_root *tree, const k_t *keys)
{
        int pos = 0;
        check(GET(tree->root), 0, ~0, keys, &pos);
}

static int
cmpKey(const void *p1, const void *p2)
{
        k_t k1 = *(const k_t*)p1, k2 = *(const k_t*)p2;
        if (k1 < k2)
                return -1;
        if (k1 > k2)
                return 1;
        return 0;
}

#if KERNEL
#include <linux/random.h>
#include <linux/sort.h>
#include <linux/vmalloc.h>

static int
test(void)
{
        enum { LEN = 10000, DEL = 50 };
        struct cb_root tree = {};
        k_t *keys;
        int i, insertFrees, deleteFrees;

        keys = vmalloc(LEN * sizeof *keys);
        assert(keys);
        
        for (i = 0; i < LEN; ++i) {
                k_t key = get_random_int();
                //printf("+++ %d\n", val);
                TreeBB_Insert(&tree, key, 0);
                keys[i] = key;
        }
        insertFrees = totalFreed;
        totalFreed = 0;

        for (i = 0; i < DEL; ++i) {
                //printf("--- %d\n", vals[i]);
                TreeBB_Delete(&tree, keys[i]);
        }
        deleteFrees = totalFreed;
        totalFreed = 0;

        sort(keys+DEL, LEN-DEL, sizeof keys[0], cmpKey, NULL);
        assert(nodeSize(GET(tree.root)) == LEN-DEL);
        TreeBB_Check(&tree, keys+DEL);
        printk(KERN_INFO "***\n");
        printk(KERN_INFO "%d freed by %d inserts, %d freed by %d deletes\n",
               insertFrees, LEN, deleteFrees, DEL);
        printk(KERN_INFO "***\n");
        return 0;
}

module_init(test);
#else
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
