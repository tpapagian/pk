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

#include <trace/events/sched.h>

#include "forp.h"

DEFINE_MUTEX(forp_mu);
DEFINE_PER_CPU_ALIGNED(struct forp_rec[FORP_REC_SIZE], forp_recs);
DEFINE_PER_CPU_ALIGNED(struct forp_rec[FORP_ENTRY_REC_SIZE], forp_entry_recs);
struct forp_label *forp_labels __read_mostly;

int forp_rec_num __read_mostly;
int forp_enable __read_mostly;

void forp_start_entry(unsigned long entry)
{
	if (!forp_enable || !current)
		return;

	current->forp_entry_calltime = forp_time();
        current->forp_entry = entry;
        current->forp_entry_start = 1;
}

static inline void __forp_end_entry(struct task_struct *tsk)
{
        struct forp_rec *rec;
        unsigned long entry;

        if (!tsk)
                return;

        if (tsk->forp_entry_start) {
		unsigned long elp = forp_time() - tsk->forp_entry_calltime;
                entry = tsk->forp_entry;
                rec = &get_cpu_var(forp_entry_recs[entry]);
		rec->time += elp;
		rec->count++;
                put_cpu_var(rec);
        }
        tsk->forp_entry_start = 0;
}
void forp_end_entry(void)
{
        __forp_end_entry(current);
}

void forp_init_task(struct task_struct *t)
{
	t->forp_curr_stack = -1;
}

void forp_exit_task(struct task_struct *t)
{
	if (!forp_enable)
                return;

        if (current == NULL)
                return;

	__forp_end_entry(t);
        while (current->forp_curr_stack >= 0)
		forp_end();
}

static void
forp_probe_sched_switch(void *ignore, struct task_struct *prev, 
			struct task_struct *next)
{
	u64 timestamp;
	int index;

	timestamp = forp_time();

	prev->forp_timestamp = timestamp;

	/* only process tasks that we timestamped */
	if (!next->forp_timestamp)
		return;

	/*
	 * Update all the counters in next to make up for the
	 * time next was sleeping.
	 */
	timestamp -= next->forp_timestamp;

	if (next->forp_entry_start)
		next->forp_entry_calltime += timestamp; 
	for (index = next->forp_curr_stack; index >= 0; index--)
		next->forp_stack[index].calltime += timestamp;
}

int forp_init(void)
{
	int r, i, cpu;

	r = register_trace_sched_switch(forp_probe_sched_switch, NULL);
	if (r) {
		printk(KERN_ERR "Couldn't activate tracepoint"
		       " probe to kernel_sched_switch: %d\n", r);
		return r;
	}

	for_each_possible_cpu(cpu) {	
		for (i = 0; i < forp_rec_num; i++)
			forp_reset_rec(&per_cpu(forp_recs[i], cpu));
		for (i = 0; i < FORP_ENTRY_REC_SIZE; i++)
			forp_reset_rec(&per_cpu(forp_entry_recs[i], cpu));
	}

	forp_enable = 1;
	return 0;
}

void forp_deinit(void)
{
	unregister_trace_sched_switch(forp_probe_sched_switch, NULL);
	forp_enable = 0;
}

void forp_register(struct forp_label *labels, int n)
{

	mutex_lock(&forp_mu);
	if (forp_labels)
		kfree(forp_labels);
	forp_labels = labels;
	forp_rec_num = n;
	mutex_unlock(&forp_mu);
}

void forp_start(unsigned int id){
	int depth = current->forp_curr_stack + 1;
	if (forp_enable && forp_labels[id].depth == depth) {
		int i = ++current->forp_curr_stack;
		struct forp_ret_stack *f = &current->forp_stack[i];

		f->subtime = 0;
		f->calltime = forp_time();
		f->id = id;
	}
}

void forp_end(void)
{
	struct forp_ret_stack *f;	
	struct forp_rec *rec;
	int i = current->forp_curr_stack;

	if (i < 0)
		return;

	f = &current->forp_stack[i];
	rec = &__get_cpu_var(forp_recs[f->id]);
	
	/* XXX time and count should be atomic */
	rec->time += forp_time() - f->calltime;
	rec->count++;

	current->forp_curr_stack--;
}
