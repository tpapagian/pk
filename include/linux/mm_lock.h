#ifndef _LINUX_MM_LOCK_H
#define _LINUX_MM_LOCK_H

#ifdef __KERNEL__

/* mm_struct locking */

static inline void
mm_lock(struct mm_struct *mm)
{
	down_write(&mm->mmap_sem);
}

static inline void
mm_lock_nested(struct mm_struct *mm, int subclass)
{
	down_write_nested(&mm->mmap_sem, subclass);
}

static inline void
mm_unlock(struct mm_struct *mm)
{
	up_write(&mm->mmap_sem);
}

static inline void
mm_lock_read(struct mm_struct *mm)
{
	down_read(&mm->mmap_sem);
}

static inline int
mm_lock_tryread(struct mm_struct *mm)
{
	return down_read_trylock(&mm->mmap_sem);
}

static inline void
mm_unlock_read(struct mm_struct *mm)
{
	up_read(&mm->mmap_sem);
}

static inline void
mm_lock_write_to_read(struct mm_struct *mm)
{
	downgrade_write(&mm->mmap_sem);
}

static inline void
mm_lock_init(struct mm_struct *mm)
{
	init_rwsem(&mm->mmap_sem);
}

static inline int
mm_is_locked(struct mm_struct *mm)
{
	return rwsem_is_locked(&mm->mmap_sem);
}

static inline void
mm_lock_prefetch(struct mm_struct *mm)
{
	prefetchw(&mm->mmap_sem);
}

static inline void
mm_nest_spin_lock(spinlock_t *s, struct mm_struct *mm)
{
	spin_lock_nest_lock(s, &mm->mmap_sem);
}

#define INIT_MM_LOCK(mmstruct)			\
	.mmap_sem	= __RWSEM_INITIALIZER(mmstruct.mmap_sem)


/* _locked variants */

static inline unsigned long do_mmap_locked(struct file *file,
	unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long offset) 
{
	unsigned long r;
	mm_lock(current->mm);
	r = do_mmap(file, addr, len, prot, flag, offset);
	mm_unlock(current->mm);
	return r;
}

static inline int do_munmap_locked(struct mm_struct *mm, unsigned long start, size_t len)
{
	int r;
	mm_lock(mm);
	r = do_munmap(mm, start, len);
	mm_unlock(mm);
	return r;
}

static inline unsigned long do_brk_locked(unsigned long addr, unsigned long len)
{
	unsigned long r;
	mm_lock(current->mm);
	r = do_brk(addr, len);
	mm_unlock(current->mm);
	return r;
}

static inline int get_user_pages_locked(struct task_struct *tsk, struct mm_struct *mm,
			unsigned long start, int nr_pages, int write, int force,
			struct page **pages, struct vm_area_struct **vmas)
{
	int r;
	mm_lock(mm);
	r = get_user_pages(tsk, mm, start, nr_pages, write, force, pages, vmas);
	mm_unlock(mm);
	return r;
}

#endif /* __KERNEL__ */
#endif /* _LINUX_MM_LOCK_H */
