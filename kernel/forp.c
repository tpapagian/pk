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

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/forp-patch.h>
#include <linux/sched.h>

#include <trace/events/sched.h>

#define FORP_REC_SIZE 256

static DEFINE_PER_CPU_ALIGNED(struct forp_rec[FORP_REC_SIZE], forp_recs);

static struct {
	struct dentry *root;
} forp_debugfs;

void forp_init_task(struct task_struct *t)
{
	t->forp_curr_stack = -1;
}

static void
forp_probe_sched_switch(struct rq *__rq, struct task_struct *prev,
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

	for (index = next->forp_curr_stack; index >= 0; index--)
		next->forp_stack[index].calltime += timestamp;
}

void forp_init_rec(void)
{
	int ret, cpu;

	ret = register_trace_sched_switch(forp_probe_sched_switch);
	if (ret)
		printk(KERN_ERR "Couldn't activate tracepoint"
		       " probe to kernel_sched_switch\n");

	for_each_possible_cpu(cpu) {
		struct forp_rec *rec = &per_cpu(forp_recs[0], cpu);		
		rec->depth = 0;
	}	
}

void forp_start(unsigned int id)
{
	struct forp_rec *rec = &__get_cpu_var(forp_recs[id]);
	if (rec->depth == current->forp_curr_stack + 1) {
		int i = ++current->forp_curr_stack;
		struct forp_ret_stack *f = &current->forp_stack[i];

		f->subtime = 0;
		f->calltime = forp_time();
		f->id = id;
	}
}

void forp_end(void)
{
	int i = current->forp_curr_stack;
	struct forp_ret_stack *f = &current->forp_stack[i];
	struct forp_rec *rec = &__get_cpu_var(forp_recs[f->id]);
	
	/* XXX time and count should be atomic */
	rec->time += forp_time() - f->calltime;
	rec->count++;

	current->forp_curr_stack--;
}

static int forp_open_rec(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t
forp_read_rec(struct file *filp, char __user *ubuf,                                                                                                                                      
	      size_t cnt, loff_t *ppos)
{
	unsigned long cpu = (unsigned long)filp->private_data;
	char *buf, *p;
	int i, r;

	/* XXX should lock this */

	int sz = 4096;

	buf = kmalloc(sz, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	r = 0;
	p = buf;
	for (i = 0; i < FORP_REC_SIZE; i++) {
		struct forp_rec *rec = &per_cpu(forp_recs[i], cpu);
		if (rec->count)
			r += snprintf(p, sz - r, "%u %llu %llu\n",
				      rec->id, rec->time, rec->count);
		p = &buf[r];
	}

	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
	kfree(buf);
	return r;
}

static ssize_t                                                                                                                                                                                     
forp_write_rec(struct file *filp, const char __user *ubuf,
	       size_t cnt, loff_t *ppos)
{
	return -ENOSYS;
}

static const struct file_operations forp_rec_ops = {
	.open 	    = forp_open_rec,
	.read 	    = forp_read_rec,
	.write 	    = forp_write_rec,
};

static __init int forp_init_debugfs(void)
{
	unsigned long cpu;
	struct dentry *d;
	char *name;

	d = debugfs_create_dir("forp", NULL);
	if (!d) {
		printk(KERN_ERR "Could not create debugfs directory 'forp'\n");
		return -ENOMEM;
	}
	forp_debugfs.root = d;

	for_each_possible_cpu(cpu) {
		name = kmalloc(32, GFP_KERNEL);
		if (!name) {
			WARN(1, "Could not allocate per-cpu file\n");
			return -ENOMEM;
		}

		snprintf(name, 32, "forp%lu", cpu);

		if (debugfs_create_file(name, 0644, d, (void *)cpu, &forp_rec_ops) == NULL) {
			WARN(1, "Could not create per-cpu file\n");
			kfree(name);
			return -ENOMEM;
		}
	}

#if 0
	if (debugfs_create_file("forp-all", 0644, d, NULL, &forp_all_rec_ops) == NULL) {
		WARN(1, "Could not create summary file\n");
		return -ENOMEM;
	}
#endif

	forp_init_rec();

	return 0;
}
fs_initcall(forp_init_debugfs);
