// -*- indent-tabs-mode: nil -*-
#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/cbtree.h>

#define assert(s) BUG_ON(!(s))
#else
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

struct cb_root
{
        struct TreeBB_Node *root;
};

struct cb_kv
{
	uintptr_t key;
	void *value;
};

#define smp_wmb() __asm__ __volatile__("": : :"memory")

#define rcu_assign_pointer(p, v) \
	({ \
		if (!__builtin_constant_p(v) || \
		    ((v) != NULL)) \
			smp_wmb(); \
		(p) = (typeof(*v)*)(v); \
	})

#endif

#define TEST 1

// Sequential operations
#define SHARED(typ, name) struct { typ val; } name
#define SET(sh, v) ((sh).val = (v))
#define GET(sh) ((const __typeof__((sh).val)) (sh).val)

/******************************************************************
 * Tree types
 */

enum { INPLACE = 1 };

typedef uintptr_t k_t;

typedef struct cb_kv kv_t;

struct TreeBB_Node
{
        SHARED(struct TreeBB_Node *, left);
        SHARED(struct TreeBB_Node *, right);
        SHARED(unsigned int, size);

        kv_t kv;

#ifdef __KERNEL__
        struct rcu_head rcu;
#endif
};

typedef struct TreeBB_Node node_t;

static inline node_t*
nodeOf(kv_t *kv)
{
        return (node_t*)((char*)kv - offsetof(node_t, kv));
}

/******************************************************************
 * User/kernel adaptors
 */

#if TEST
static int totalFreed;
#endif

#ifdef __KERNEL__

static struct kmem_cache *node_cache;

static int __init
TreeBBInit(void)
{
        node_cache = kmem_cache_create("struct TreeBB_Node",
                                       sizeof(node_t), 0, SLAB_PANIC, NULL);
        return 0;
}

core_initcall(TreeBBInit);

static node_t *
TreeBBNewNode(void)
{
        return kmem_cache_alloc(node_cache, GFP_ATOMIC);
}

static void
__TreeBBFreeNode(struct rcu_head *rcu)
{
        node_t *n = container_of(rcu, node_t, rcu);
        kmem_cache_free(node_cache, n);
}

static void
TreeBBFreeNode(node_t *n)
{
        call_rcu(&n->rcu, __TreeBBFreeNode);
#if TEST
        totalFreed++;
#endif
}

#else

static node_t *
TreeBBNewNode(void)
{
        return malloc(sizeof(node_t));
}

static void
TreeBBFreeNode(node_t *n)
{
        free(n);
#if TEST
        totalFreed++;
#endif
}

#endif

/******************************************************************
 * Tree algorithms
 */

enum { WEIGHT = 4 };

static inline int
nodeSize(node_t *node)
{
        return node ? GET(node->size) : 0;
}

static inline node_t *
mkNode(node_t *left, node_t *right, kv_t *kv)
{
        node_t *node = TreeBBNewNode();
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
        node_t *res = mkNode(mkNode(left, GET(right->left), kv),
                             GET(right->right), &right->kv);
        TreeBBFreeNode(right);
        return res;
}

static node_t *
doubleL(node_t *left, node_t *right, kv_t *kv)
{
//        printf("%s\n", __func__);
        node_t *res = mkNode(mkNode(left, GET(GET(right->left)->left), kv),
                             mkNode(GET(GET(right->left)->right),
                                    GET(right->right),
                                    &right->kv),
                             &GET(right->left)->kv);
        TreeBBFreeNode(GET(right->left));
        TreeBBFreeNode(right);
        return res;
}

static node_t *
singleR(node_t *left, node_t *right, kv_t *kv)
{
//        printf("%s\n", __func__);
        node_t *res = mkNode(GET(left->left),
                             mkNode(GET(left->right), right, kv),
                             &left->kv);
        TreeBBFreeNode(left);
        return res;
}

static node_t *
doubleR(node_t *left, node_t *right, kv_t *kv)
{
//        printf("%s\n", __func__);
        node_t *res = mkNode(mkNode(GET(left->left),
                                    GET(GET(left->right)->left),
                                    &left->kv),
                             mkNode(GET(GET(left->right)->right), right, kv),
                             &GET(left->right)->kv);
        TreeBBFreeNode(GET(left->right));
        TreeBBFreeNode(left);
        return res;
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
        node_t *res;

        if (ln+rn < 2)
                goto balanced;
        if (rn > WEIGHT * ln)
                res = mkBalancedL(left, right, kv);
        else if (ln > WEIGHT * rn)
                res = mkBalancedR(left, right, kv);
        else
                goto balanced;

        TreeBBFreeNode(cur);
        return res;

balanced:
        if (inPlace) {
                // When updating in-place, we only ever modify one of
                // the two pointers as our visible write.  We also
                // modify the size, but this is okay because size is
                // never in a reader's read set.
                if (replace == 0) {
                        assert(GET(cur->right) == right);
                        // XXX rcu_assign_pointer
			smp_wmb();
                        SET(cur->left, left);
                } else {
                        assert(GET(cur->left) == left);
                        // XXX rcu_assign_pointer
			smp_wmb();
                        SET(cur->right, right);
                }
                SET(cur->size, 1 + nodeSize(left) + nodeSize(right));
                return cur;
        } else {
                res = mkNode(left, right, kv);
                TreeBBFreeNode(cur);
                return res;
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
        node_t *nroot = insert(tree->root, &kv);
        rcu_assign_pointer(tree->root, nroot);
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
delete(node_t *node, k_t key, void **deleted)
{
        node_t *min, *left, *right;

        if (!node) {
                *deleted = NULL;
                return NULL;
        }

        left = GET(node->left);
        right = GET(node->right);
        if (key < node->kv.key)
                return mkBalanced(node, delete(left, key, deleted), right, 0, INPLACE);
        if (key > node->kv.key)
                return mkBalanced(node, left, delete(right, key, deleted), 1, INPLACE);

        // We found our node to delete
        *deleted = node->kv.value;
        TreeBBFreeNode(node);
        if (!left)
                return right;
        if (!right)
                return left;
        right = deleteMin(right, &min);
        // This needs to be performed non-destructively because the
        // min element is still linked in to the tree below us.  Thus,
        // we need to create a new min element here, which will be
        // atomically swapped in to the tree by our parent (along with
        // the new subtree where the min element has been removed).
        return mkBalanced(min, left, right, 1, false);
}

void *
TreeBB_Delete(struct cb_root *tree, uintptr_t key)
{
        void *deleted;
        node_t *nroot = delete(tree->root, key, &deleted);
        rcu_assign_pointer(tree->root, nroot);
        return deleted;
}

struct cb_kv *
TreeBB_Find(struct cb_root *tree, uintptr_t needle)
{
        node_t *node;

        node = tree->root;
        while (node) {
                if (node->kv.key == needle)
                        break;
                else if (node->kv.key > needle)
                        node = GET(node->left);
                else
                        node = GET(node->right);
        }
        return node ? &node->kv : NULL;
}

struct cb_kv *
TreeBB_FindGT(struct cb_root *tree, uintptr_t needle)
{
        node_t *node = tree->root;
        node_t *res = NULL;

        while (node) {
                if (node->kv.key > needle) {
                        res = node;
                        node = GET(node->left);
                } else {
                        node = GET(node->right);
                }
        }
        return res ? &res->kv : NULL;
}

struct cb_kv *
TreeBB_FindLE(struct cb_root *tree, uintptr_t needle)
{
        node_t *node = tree->root;
        node_t *res = NULL;

        while (node) {
                if (node->kv.key == needle)
                        return &node->kv;

                if (node->kv.key > needle) {
                        node = GET(node->left);
                } else {
                        res = node;
                        node = GET(node->right);
                }
        }
        return res ? &res->kv : NULL;
}

static void
foreach(node_t *node, void (*cb)(struct cb_kv *))
{
        if (!node)
                return;
        foreach(GET(node->left), cb);
        cb(&node->kv);
        foreach(GET(node->right), cb);
}

void
TreeBB_ForEach(struct cb_root *tree, void (*cb)(struct cb_kv *))
{
        foreach(tree->root, cb);
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

static void __attribute__((__used__))
show(node_t *node, int depth)
{
        if (!node)
                return;

        show(GET(node->left), depth + 1);
#if __KERNEL__
        printk("%*s%p -> %p\n", depth*2, "", (void*)node->kv.key, node->kv.value);
#else
        printf("%*s%p -> %p\n", depth*2, "", (void*)node->kv.key, node->kv.value);
#endif
        show(GET(node->right), depth + 1);
}

static void
TreeBB_Check(struct cb_root *tree, const k_t *keys)
{
        int pos = 0;
        check(tree->root, 0, ~0, keys, &pos);
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

#if TEST
#ifdef __KERNEL__
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
        assert(nodeSize(tree.root) == LEN-DEL);
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
        struct cb_root tree = {};
        k_t keys[LEN];
        int i;

        for (i = 0; i < LEN; ++i) {
                k_t key = rand();
                printf("+++ %d\n", key);
                TreeBB_Insert(&tree, key, (void*)key);
                keys[i] = key;
        }
        int insertFrees = totalFreed;
        totalFreed = 0;

        for (i = 0; i < DEL; ++i) {
                printf("--- %d\n", keys[i]);
                assert(TreeBB_Delete(&tree, keys[i]) == (void*)keys[i]);
                assert(TreeBB_Delete(&tree, keys[i]) == NULL);
        }
        int deleteFrees = totalFreed;
        totalFreed = 0;

        qsort(keys+DEL, LEN-DEL, sizeof keys[0], cmpKey);
        assert(nodeSize(tree.root) == LEN-DEL);
        TreeBB_Check(&tree, keys+DEL);

        for (i = DEL; i < LEN; ++i) {
                void *next = i < LEN - 1 ? (void*)keys[i+1] : NULL;
                void *this = (void*)keys[i];
                void *prev = i > DEL ? (void*)keys[i-1] : NULL;

                // XXX Assumes no two keys are consecutive
#define V(x) ({typeof(x) __x = (x); __x ? __x->value : NULL;})
                assert(V(TreeBB_Find(&tree, keys[i] - 1)) == NULL);
                assert(V(TreeBB_Find(&tree, keys[i])) == this);
                assert(V(TreeBB_Find(&tree, keys[i] + 1)) == NULL);
                assert(V(TreeBB_FindGT(&tree, keys[i] - 1)) == this);
                assert(V(TreeBB_FindGT(&tree, keys[i])) == next);
                assert(V(TreeBB_FindGT(&tree, keys[i] + 1)) == next);
                assert(V(TreeBB_FindLE(&tree, keys[i] - 1)) == prev);
                assert(V(TreeBB_FindLE(&tree, keys[i])) == this);
                assert(V(TreeBB_FindLE(&tree, keys[i] + 1)) == this);
        }

        printf("%d freed by %d inserts, %d freed by %d deletes\n",
               insertFrees, LEN, deleteFrees, DEL);
        return 0;
}
#endif  /* !__KERNEL__ */
#endif  /* TEST */
