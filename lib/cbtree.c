/*
 * Concurrent balanced tree
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cbtree.h>
#include <linux/module.h>


/*
 * Balanced tree: Implementation based on
 * http://groups.csail.mit.edu/mac/users/adams/BB/, which makes
 * supporting concurrent reads with locked-writes easy, because
 * updates are non-destructive.  As an optimization, we turn the easy
 * cases into destructive updates.
 */

#define WEIGHT 4
#define SIZE(n) ((n == 0) ? 0 : n->size)
#define assert(a) if (a) panic("cbtree")

typedef struct cb_node node_t;
static struct kmem_cache *cb_node_cache;

node_t*
alloc_node(int key, node_t *l, node_t *r)
{
  node_t *n = kmem_cache_alloc(cb_node_cache, GFP_ATOMIC);
  assert(n);
  n->key = key;
  n->size = SIZE(l) + SIZE(r) + 1;
  n->l = l;
  n->r = r;
  return n;
}

void
update_node(node_t *n, int s, node_t *l, node_t *r)
{
  n->size = s;
  n->l = l;
  n->r = r;
}

void
free_node(void *p)
{
  node_t *n = (node_t *) p;
  // printf("free_node: 0x%lx\n", (long) n);
  assert(n != 0);
}

void
rcu_free_node(node_t *n)
{
  // XXX
}

void 
tree_printspace(int s)
{
  int i;
  
  for (i = 0; i < s; i++)
    printk(" ");
}

void
tree_print(node_t *n, int depth)
{
  if (n == 0) return;

  if (n->l != 0)
    tree_print(n->l, depth+1);
  tree_printspace(depth*2);
  printk("0x%lx: %d sz %d l 0x%p 0x%p\n", (long) n, n->key, n->size, 
	 (void *) n->l, (void *) n->r);
  if (n->r != 0)
    tree_print(n->r, depth+1);
}

node_t*
tree_search(node_t *n, int key)
{
  if (n == 0) return 0;

  if (n->key == key) return n;

  if (key < n->key) return tree_search(n->l, key);
  else return tree_search(n->r, key);
}

node_t* tree_balance(node_t *n, node_t *l, node_t *r, int, int);

node_t*
tree_single_left(int key, node_t *l, node_t *r)
{
  node_t *t = alloc_node(r->key, alloc_node(key, l, r->l), r->r);
  rcu_free_node(r);
  return t;
}

node_t*
tree_double_left(node_t *n, node_t *l, node_t *r)
{
  node_t *tl = alloc_node(n->key, l,  r->l->l);
  // node_t *tr = alloc_node(r->key, r->l->r, r->r);
  node_t *tr = tree_balance(r, r->l->r, r->r, 0, 0);
  node_t *t = alloc_node(r->l->key, tl, tr);

  rcu_free_node(r->l);
  // rcu_free_node(r);
  return t;
}

node_t*
tree_single_right(int key, node_t *l, node_t *r)
{
  node_t *t = alloc_node(l->key, l->l, alloc_node(key, l->r, r));
  rcu_free_node(l);
  return t;
}

node_t*
tree_double_right(node_t *n, node_t *l, node_t *r)
{
  // node_t *tl = alloc_node(l->key, l->l, l->r->l);
  node_t *tl = tree_balance(l, l->l, l->r->l, 0, 0);
  node_t *tr = alloc_node(n->key, l->r->r, r);
  node_t *t = alloc_node(l->r->key, tl, tr);

  rcu_free_node(l->r);
  // rcu_free_node(l);
  return t;
}

node_t*
tree_node(node_t *n, node_t *l, node_t *r, int leftreplace, int inplace)
{
  node_t *t;

  if (inplace) {
    if (leftreplace) {
      assert(n->r == r);
      n->l = l;
    } else {
      assert(n->l == l);
      n->r = r;
    }
    n->size = 1 + SIZE(l) + SIZE(r);
    t = n;
  } else {
    t = alloc_node(n->key, l, r);
    rcu_free_node(n);
  }
  return t;
}

/* Returns a balanced tree for n after r or l have grown by one. */
node_t*
tree_balance(node_t *n, node_t *l, node_t *r, int leftreplace, int inplace)
{
  int ln = SIZE(l);
  int rn = SIZE(r);
  node_t *t;

  //  printf("tree_balance: (%d,%d,0x%lx, 0x%lx) leftr %d inplace %d\n", n->key, 
  //	 n->val, (long) l, (long) r, leftreplace, inplace);
  if (ln + rn < 2) {
    t = tree_node(n, l, r, leftreplace, inplace);
  } else if (rn > WEIGHT * ln) {
    int rln = SIZE(r->l);
    int rrn = SIZE(r->r);
    if (rln < rrn) t = tree_single_left(n->key, l, r);
    else t = tree_double_left(n, l, r);
    rcu_free_node(n);
  } else if (ln > WEIGHT * rn) {
    int lln = SIZE(l->l);
    int lrn = SIZE(l->r);
    if (lrn < lln) t = tree_single_right(n->key, l, r);
    else t = tree_double_right(n, l, r);
    rcu_free_node(n);
  } else {
    t = tree_node(n, l, r, leftreplace, inplace);
  }
  return t;
}

node_t*
tree_add(node_t *n, int key)
{
  node_t* t;
  if (n == 0) {
    t = alloc_node(key, 0, 0);
  } else if (key == n->key) {
    t = n;
  } else if (key < n->key) {
    t = tree_balance(n, tree_add(n->l, key), n->r, 1, 1);
  } else {
    t = tree_balance(n, n->l, tree_add(n->r, key), 0, 1);
  }
  return t;
}

node_t* 
tree_min(node_t *n)
{
  if (n->l == 0) return n;
  else return tree_min(n->l);
}

node_t*
tree_delmin(node_t *n)
{
  node_t *t;

  assert(n);
  if (n->l == 0) {
    t = n->r;
  } else {
    /* rebuild branch with min non-destructively, because readers
     * might be going and must see min */
    t = tree_balance(n, tree_delmin(n->l), n->r, 1, 0);
  }
  return t;
}

node_t*
tree_delprime(node_t *l, node_t *r)
{
  node_t *t;
  if (l == 0) t = r;
  else if (r == 0) t = l;
  else {
    node_t *min = tree_min(r);   // XXX combine with tree_delmin?
    assert(min->l == 0);
    /* min is still linked below in the tree, so build new min
     * non-destructively. the new min will be inserted atomically by
     * parent. */
    t = tree_balance(min, l, tree_delmin(r), 0, 0);
  }
  return t;
}

node_t*
tree_del(node_t *n, int key)
{
  node_t *t;

  if (n == 0) t = 0;
  else if (key < n->key) {
    t = tree_balance(n, tree_del(n->l, key), n->r, 1, 1);
  } else if (key > n->key) {
    t = tree_balance(n, n->l, tree_del(n->r, key), 0, 1);
  } else { 
    t = tree_delprime(n->l, n->r);
    rcu_free_node(n);
  }
  return t;
}

