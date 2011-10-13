#ifndef _ASM_MCS_H_
#define _ASM_MCS_H_

struct qnode {
        volatile void *next;
        volatile char locked;
        char __pad[0] __attribute__((aligned(64)));
};

typedef struct qnode mcs_arg_t;

typedef struct {
        struct qnode *v __attribute__((aligned(64)));
} mcslock_t;

static void inline
mcs_init(mcslock_t *l)
{
        l->v = NULL;
}

static void inline
mcs_lock(mcslock_t *l, volatile struct qnode *mynode)
{
        struct qnode *predecessor;

        mynode->next = NULL;
        predecessor = (struct qnode *)xchg((long *)&l->v, (long)mynode);

        if (predecessor) {
                mynode->locked = 1;
                barrier();
                predecessor->next = mynode;
                while (mynode->locked)
                        cpu_relax();
        }
}

static int inline
mcs_trylock(mcslock_t *l, volatile struct qnode *mynode)
{
        long r;

        mynode->next = NULL;
        r = cmpxchg((long *)&l->v, 0, (long)mynode);
        return r == 0;
}

static void inline
mcs_unlock(mcslock_t *l, volatile struct qnode *mynode)
{
        if (!mynode->next) {
                if (cmpxchg((long *)&l->v, (long)mynode, 0) == (long)mynode)
                        return;
                while (!mynode->next)
                        cpu_relax();
        }
        ((struct qnode *)mynode->next)->locked = 0;
}

static inline void assert_mcs_locked(mcslock_t *l)
{
        /* XXX */
}

static inline void mcs_lock_nested(mcslock_t *l,
                                   volatile struct qnode *mynode,
                                   int subclass)
{
        /* XXX */
        mcs_lock(l, mynode);
}

#endif
