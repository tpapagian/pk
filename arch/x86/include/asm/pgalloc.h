#ifndef _ASM_X86_PGALLOC_H
#define _ASM_X86_PGALLOC_H

#include <linux/threads.h>
#include <linux/mm.h>		/* for struct page */
#include <linux/pagemap.h>

static inline int  __paravirt_pgd_alloc(struct mm_struct *mm) { return 0; }

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define paravirt_pgd_alloc(mm)	__paravirt_pgd_alloc(mm)
static inline void paravirt_pgd_free(struct mm_struct *mm, pgd_t *pgd) {}
static inline void paravirt_alloc_pte(struct mm_struct *mm, unsigned long pfn)	{}
static inline void paravirt_alloc_pmd(struct mm_struct *mm, unsigned long pfn)	{}
static inline void paravirt_alloc_pmd_clone(unsigned long pfn, unsigned long clonepfn,
					    unsigned long start, unsigned long count) {}
static inline void paravirt_alloc_pud(struct mm_struct *mm, unsigned long pfn)	{}
static inline void paravirt_release_pte(unsigned long pfn) {}
static inline void paravirt_release_pmd(unsigned long pfn) {}
static inline void paravirt_release_pud(unsigned long pfn) {}
#endif

/*
 * Flags to use when allocating a user page table page.
 */
extern gfp_t __userpte_alloc_gfp;

/*
 * Allocate and free page tables.
 */
extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

extern pte_t *pte_alloc_one_kernel(struct mm_struct *, unsigned long);
extern pgtable_t pte_alloc_one(struct mm_struct *, unsigned long);

/* Should really implement gc for free page table pages. This could be
   done with a reference count in struct page. */

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	BUG_ON((unsigned long)pte & (PAGE_SIZE-1));
	free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, struct page *pte)
{
	pgtable_page_dtor(pte);
	__free_page(pte);
}

extern void ___pte_free_tlb(struct mmu_gather *tlb, struct page *pte);

static inline void __pte_free_tlb(struct mmu_gather *tlb, struct page *pte,
				  unsigned long address)
{
	___pte_free_tlb(tlb, pte);
}

static inline void pmd_populate_kernel(struct mm_struct *mm,
				       pmd_t *pmd, pte_t *pte)
{
	paravirt_alloc_pte(mm, __pa(pte) >> PAGE_SHIFT);
	set_pmd(pmd, __pmd(__pa(pte) | _PAGE_TABLE));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				struct page *pte)
{
	unsigned long pfn = page_to_pfn(pte);

	paravirt_alloc_pte(mm, pfn);
	set_pmd(pmd, __pmd(((pteval_t)pfn << PAGE_SHIFT) | _PAGE_TABLE));
}

#ifdef CONFIG_AMDRAGON_ATOMIC_PGD_UPDATE
static inline int pmd_populate_if_clear(struct mm_struct *mm, pmd_t *pmd, struct page *pte)
{
	unsigned long pfn = page_to_pfn(pte);
	pmd_t val = __pmd(((pteval_t)pfn << PAGE_SHIFT) | _PAGE_TABLE);
	pmd_t cur = cmpxchg(pmd, native_make_pmd(0), val);
	// All we actually care about is the present bit, so if we
	// failed, try an atomic update after checking the present
	// bit.
	while (val.pmd != cur.pmd) {
		if (pmd_present(cur))
			return 0;
		cur = cmpxchg(pmd, cur, val);
	}
	return 1;
}
#endif

#define pmd_pgtable(pmd) pmd_page(pmd)

#if PAGETABLE_LEVELS > 2
static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return (pmd_t *)get_zeroed_page(GFP_KERNEL|__GFP_REPEAT);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	BUG_ON((unsigned long)pmd & (PAGE_SIZE-1));
	free_page((unsigned long)pmd);
}

extern void ___pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd);

static inline void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd,
				  unsigned long adddress)
{
	___pmd_free_tlb(tlb, pmd);
}

#ifdef CONFIG_X86_PAE
extern void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmd);
#else	/* !CONFIG_X86_PAE */
static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	paravirt_alloc_pmd(mm, __pa(pmd) >> PAGE_SHIFT);
	set_pud(pud, __pud(_PAGE_TABLE | __pa(pmd)));
}

#ifdef CONFIG_AMDRAGON_ATOMIC_PGD_UPDATE
static inline int pud_populate_if_clear(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	pud_t val = __pud(_PAGE_TABLE | __pa(pmd));
	pud_t cur = cmpxchg(pud, native_make_pud(0), val);
	// All we actually care about is the present bit, so if we
	// failed, try an atomic update after checking the present
	// bit.
	while (val.pud != cur.pud) {
		if (pud_present(cur))
			return 0;
		cur = cmpxchg(pud, cur, val);
	}
	return 1;
}
#endif
#endif	/* CONFIG_X86_PAE */

#if PAGETABLE_LEVELS > 3
static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pud_t *pud)
{
	paravirt_alloc_pud(mm, __pa(pud) >> PAGE_SHIFT);
	set_pgd(pgd, __pgd(_PAGE_TABLE | __pa(pud)));
}

#ifdef CONFIG_AMDRAGON_ATOMIC_PGD_UPDATE
static inline int pgd_populate_if_clear(struct mm_struct *mm, pgd_t *pgd, pud_t *pud)
{
	pgd_t val = __pgd(_PAGE_TABLE | __pa(pud));
	pgd_t cur = cmpxchg(pgd, native_make_pgd(0), val);
	// All we actually care about is the present bit, so if we
	// failed, try an atomic update after checking the present
	// bit.
	while (val.pgd != cur.pgd) {
		if (pgd_present(cur))
			return 0;
		cur = cmpxchg(pgd, cur, val);
	}
	return 1;
}
#endif

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return (pud_t *)get_zeroed_page(GFP_KERNEL|__GFP_REPEAT);
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	BUG_ON((unsigned long)pud & (PAGE_SIZE-1));
	free_page((unsigned long)pud);
}

extern void ___pud_free_tlb(struct mmu_gather *tlb, pud_t *pud);

static inline void __pud_free_tlb(struct mmu_gather *tlb, pud_t *pud,
				  unsigned long address)
{
	___pud_free_tlb(tlb, pud);
}

#endif	/* PAGETABLE_LEVELS > 3 */
#endif	/* PAGETABLE_LEVELS > 2 */

#endif /* _ASM_X86_PGALLOC_H */
