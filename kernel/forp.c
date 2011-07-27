/*
 * Copyright (C) 2010 Silas Boyd-Wickizer
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <sbw@mit.edu>
 */

#include <linux/debugfs.h>
#include <linux/forp-patch.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <asm/idle.h>

#include <trace/events/sched.h>

#include "forp.h"

DEFINE_MUTEX(forp_mu);
DEFINE_PER_CPU_ALIGNED(struct forp_rec[FORP_REC_SIZE], forp_recs);
struct forp_label *forp_dyn_labels __read_mostly;

int forp_dyn_label_num __read_mostly;
int forp_enable __read_mostly;
unsigned long forp_flags __read_mostly;

static unsigned long static_enable;
static unsigned long static_to_id[sizeof(static_enable)];

static void __forp_pop_stack(struct forp_stack *stack);

static inline u64 forp_time(void)
{
	return __native_read_tsc();
}

static inline void forp_stamp(struct forp_call_stamp *f, unsigned long id)
{
	f->calltime = forp_time();
	f->sched = 0;
	f->irq = 0;
	f->id = id;
}

static inline void __forp_add_stamp(struct forp_call_stamp *f)
{
        struct forp_rec *rec;

	rec = &__get_cpu_var(forp_recs[f->id]);
	
	/* XXX time and count should be atomic */
	rec->time += forp_time() - f->calltime;
	rec->count++;
	rec->sched += f->sched;
	rec->irq += f->irq;
}

void forp_stamp_static(unsigned long static_id, struct forp_call_stamp *f)
{
	if ((forp_enable & FORP_ENABLE_DYN) && 
	    (static_enable & (1 << static_id))) 
	{
		forp_stamp(f, static_to_id[static_id]);
	} else {
		f->id = ~0;
	}
}

void forp_add_stamp(struct forp_call_stamp *f)
{
	if (f->id < forp_dyn_label_num)
		__forp_add_stamp(f);
}

static inline void __forp_start_entry(unsigned long entry, 
				      struct forp_stack *stack)
{
	if (forp_enable & FORP_ENABLE_ENTRY) {
		stack->forp_entry.calltime = forp_time();
		stack->forp_entry.sched = 0;
		stack->forp_entry.irq = 0;
		stack->forp_entry.id = entry;
		stack->forp_entry_start = 1;
	}
}

void forp_start_entry(unsigned long entry)
{
	struct forp_stack *stack;

	if (!current)
		return;

	stack = current->forp_in_irq ?
		&current->forp_irq_stack : &current->forp_call_stack;

	__forp_start_entry(entry, stack);
}

static inline void __forp_end_entry(struct forp_stack *stack)
{
        struct forp_rec *rec;
        unsigned long entry;

        if (!(forp_enable & FORP_ENABLE_ENTRY))
                return;

        if (stack->forp_entry_start) {
		unsigned long elp = forp_time() - stack->forp_entry.calltime;
		/* Entry recs start after Dynamic recs */
                entry = stack->forp_entry.id + FORP_DYN_REC_SIZE;
                rec = &get_cpu_var(forp_recs[entry]);
		rec->time += elp;
		rec->count++;
		rec->sched += stack->forp_entry.sched;
		rec->irq += stack->forp_entry.irq;
                put_cpu_var(rec);
        }
        stack->forp_entry_start = 0;
}

void forp_end_entry(void)
{
	struct forp_stack *stack = current->forp_in_irq ?
		&current->forp_irq_stack : &current->forp_call_stack;
        __forp_end_entry(stack);
}

void forp_init_task(struct task_struct *t)
{
	t->forp_call_stack.depth = -1;
	t->forp_irq_stack.depth = -1;
}

void forp_exit_task(struct task_struct *t)
{
	if (!forp_enable)
                return;

        if (current == NULL)
                return;

	if (t)
		__forp_end_entry(&t->forp_call_stack);
        while (current->forp_call_stack.depth >= 0)
		__forp_pop_stack(&current->forp_call_stack);

	WARN_ON(current->forp_irq_stack.depth >= 0);
}

void forp_sched_switch(struct task_struct *prev, struct task_struct *next)
{
	u64 timestamp;
	int index;
	struct forp_stack *pstack = &prev->forp_call_stack;
	struct forp_stack *nstack = &next->forp_call_stack;

	WARN_ON(prev->forp_in_irq);

	if (pstack->forp_entry_start)
		pstack->forp_entry.sched++;
	for (index = pstack->depth; index >= 0; index--)
		pstack->forp_stack[index].sched++;

	if (forp_flags & FORP_FLAG_SLEEP_TIME)
		return;

	timestamp = forp_time();
	pstack->forp_switchstamp = timestamp;

	/* only process tasks that we timestamped */
	if (!nstack->forp_switchstamp)
		return;

	/*
	 * Update all the counters in next to make up for the
	 * time next was sleeping.
	 */
	timestamp -= nstack->forp_switchstamp;

	if (nstack->forp_entry_start)
		nstack->forp_entry.calltime += timestamp; 
	for (index = nstack->depth; index >= 0; index--)
		nstack->forp_stack[index].calltime += timestamp;
}

void forp_irq_enter(int irq)
{
	struct forp_stack *cstack = &current->forp_call_stack;
	struct forp_stack *istack = &current->forp_irq_stack;
	u64 timestamp;
	int index;

	WARN_ON(current->forp_in_irq);

	if (cstack->forp_entry_start)
		cstack->forp_entry.irq++;
	for (index = cstack->depth; index >= 0; index--)
		cstack->forp_stack[index].irq++;

	timestamp = forp_time();
	istack->forp_switchstamp = timestamp;
	current->forp_in_irq = 1;

	forp_start_entry(FORP_HI_SOFTIRQ + irq);
}

void forp_irq_exit(void)
{
	struct forp_stack *cstack = &current->forp_call_stack;
	struct forp_stack *istack = &current->forp_irq_stack;
	u64 timestamp;
	int index;

	WARN_ON(!current->forp_in_irq);

	forp_end_entry();

	/*
	 * Update all the counters in current to make up for the
	 * time processing irqs.
	 */
	timestamp = forp_time();
	timestamp -= istack->forp_switchstamp;

	if (cstack->forp_entry_start)
		cstack->forp_entry.calltime += timestamp; 
	for (index = cstack->depth; index >= 0; index--)
		cstack->forp_stack[index].calltime += timestamp;

	current->forp_in_irq = 0;
}

static int idle_notifier(struct notifier_block *this, 
			 unsigned long event, void *ptr)
{
	/* We are executing in an atomic context (an atomic notifier) */
	switch(event) {
	case IDLE_START:
		__forp_start_entry(FORP_ENTRY_IDLE, 
				   &idle_task(smp_processor_id())->forp_call_stack);
		break;
	case IDLE_END:
		__forp_end_entry(&idle_task(smp_processor_id())->forp_call_stack);
		break;
	default:
		printk_once(KERN_WARNING "forp: idle_notifier event %lu\n", 
			    event);
		break;
	}
	return NOTIFY_DONE;
}

struct notifier_block idle_block = {
	.notifier_call = idle_notifier,
};

int forp_init(int enable)
{
	struct task_struct *g, *t;
	unsigned long flags;
	int i, cpu;

	idle_notifier_register(&idle_block);

	for_each_possible_cpu(cpu) {	
		for (i = 0; i < FORP_REC_SIZE; i++)
			forp_reset_rec(&per_cpu(forp_recs[i], cpu));
	}

	/* reset per-task state */
	read_lock_irqsave(&tasklist_lock, flags);
	do_each_thread(g, t) {
		t->forp_call_stack.depth = -1;
		t->forp_call_stack.forp_entry_start = 0;
		t->forp_irq_stack.depth = -1;
		t->forp_irq_stack.forp_entry_start = 0;
	} while_each_thread(g, t);
	read_unlock_irqrestore(&tasklist_lock, flags);

	for_each_online_cpu(cpu) {
		idle_task(cpu)->forp_call_stack.depth = -1;
		idle_task(cpu)->forp_call_stack.forp_entry_start = 0;
		idle_task(cpu)->forp_irq_stack.depth = -1;
		idle_task(cpu)->forp_irq_stack.forp_entry_start = 0;
	}

	forp_enable = enable;
	return 0;
}

void forp_deinit(void)
{
	idle_notifier_unregister(&idle_block);
	forp_enable = 0;
}

static int forp_label_static_id(const char *name)
{
	if (!strcmp(name, "forp_schedule"))
		return FORP_STATIC_SCHEDULE;
	return -1;
}

void forp_register(struct forp_label *labels, int n)
{
	int i;

	mutex_lock(&forp_mu);
	if (forp_dyn_labels)
		kfree(forp_dyn_labels);
	forp_dyn_labels = labels;

	/* Check for labels that refer to static instrumentation */
	static_enable = 0;
	for (i = 0; i < n; i++) {
		int r = forp_label_static_id(labels[i].name);
		if (r >= 0) {
			static_enable |= (1 << r);
			static_to_id[r] = i;
		}
	}

	forp_dyn_label_num = n;
	mutex_unlock(&forp_mu);
}

static forp_flags_t __forp_push_stack(unsigned int id, struct forp_stack *stack)
{
	int depth = stack->depth + 1;
	/* XXX could easily index off the end of forp_dyn_labels ...*/
	if ((forp_enable & FORP_ENABLE_DYN) && forp_dyn_labels[id].depth == depth) {
		int i = ++stack->depth;
		forp_stamp(&stack->forp_stack[i], id);
		return 1;
	}
	return 0;
}

forp_flags_t __forp_push(unsigned int id)
{
	struct forp_stack *stack = current->forp_in_irq ?
		&current->forp_irq_stack : &current->forp_call_stack;
	return __forp_push_stack(id, stack);
}

static void __forp_pop_stack(struct forp_stack *stack)
{
	struct forp_call_stamp *f;	
	int i;

	if (forp_enable & FORP_ENABLE_DYN) {
		i = stack->depth;
		if (i < 0)
			return;

		f = &stack->forp_stack[i];
		__forp_add_stamp(f);
		stack->depth--;
	}
}

void __forp_pop(void)
{
	struct forp_stack *stack = current->forp_in_irq ?
		&current->forp_irq_stack : &current->forp_call_stack;
	__forp_pop_stack(stack);
}
