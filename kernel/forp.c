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

static DEFINE_MUTEX(forp_mu);

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

static inline void forp_reset_rec(struct forp_rec *rec)
{
	rec->time = 0;
	rec->count = 0;
}

void forp_register(struct forp_rec *recs, int n)
{
	struct forp_rec *dst, *src;
	int i, r, cpu;

	mutex_lock(&forp_mu);

	for_each_possible_cpu(cpu) {	
		for (i = 0; i < n; i++) {
			dst = &per_cpu(forp_recs[i], cpu);
			src = &recs[i];

			forp_reset_rec(dst);
			dst->id = i;
			dst->depth = src->depth;
			dst->name = src->name;
		}

		for (; i < FORP_REC_SIZE; i++) {
			dst = &per_cpu(forp_recs[i], cpu);			
			memset(dst, 0, sizeof(*dst));
		}
	}

	r = register_trace_sched_switch(forp_probe_sched_switch);
	if (r)
		printk(KERN_ERR "Couldn't activate tracepoint"
		       " probe to kernel_sched_switch\n");
	mutex_unlock(&forp_mu);
}

void forp_unregister(void)
{
	unregister_trace_sched_switch(forp_probe_sched_switch);
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

static int forp_snprintf_recs(struct forp_rec *recs, char *buf, int sz)
{
	int r, i;
	char *p;

	r = 0;
	p = buf;

	r += snprintf(p, sz - r, "  Function                               "
		      "Hit    Time\n"
		      "  --------                               "
		      "---    ----\n");
	p += r;

	for (i = 0; i < FORP_REC_SIZE; i++) {
		struct forp_rec *rec = &recs[i];
		if (rec->count)
			r += snprintf(p, sz - r, 
				      "  %-30.30s  %10llu    %10llu\n",
				      rec->name, rec->count, rec->time);
		p = &buf[r];
	}

	return r;
}

static ssize_t forp_read_rec(struct file *filp, char __user *ubuf,
			     size_t cnt, loff_t *ppos)
{
	unsigned long cpu = (unsigned long)filp->private_data;
	char *buf;
	int r;
	/* About 256 characters per line */
	int sz = FORP_REC_SIZE * 256;

	buf = kmalloc(sz, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	mutex_lock(&forp_mu);
	r = forp_snprintf_recs(&per_cpu(forp_recs[0], cpu), buf, sz);
	mutex_unlock(&forp_mu);

	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
	kfree(buf);

	return r;
}

static ssize_t forp_write_rec(struct file *filp, const char __user *ubuf,
			      size_t cnt, loff_t *ppos)
{
	unsigned long cpu = (unsigned long)filp->private_data;
	int i;

	mutex_lock(&forp_mu);
	for (i = 0; i < FORP_REC_SIZE; i++)
		forp_reset_rec(&per_cpu(forp_recs[i], cpu));
	mutex_unlock(&forp_mu);
	return cnt;
}

static const struct file_operations forp_rec_ops = {
	.open 	    = forp_open_rec,
	.read 	    = forp_read_rec,
	.write 	    = forp_write_rec,
};

static ssize_t forp_read_all_rec(struct file *filp, char __user *ubuf,
				 size_t cnt, loff_t *ppos)
{
	int i, cpu, r;
	/* About 256 characters per line */
	int sz = FORP_REC_SIZE * 256;
	char *buf;

	struct forp_rec *recs = kzalloc(FORP_REC_SIZE * sizeof(struct forp_rec), GFP_KERNEL);
	if (recs == NULL)
		return -ENOMEM;

	buf = kmalloc(sz, GFP_KERNEL);
	if (buf == NULL) {
		kfree(recs);
		return -ENOMEM;
	}

	mutex_lock(&forp_mu);

	for (i = 0; i < FORP_REC_SIZE; i++) {
		for_each_possible_cpu(cpu) {
			struct forp_rec *r = &per_cpu(forp_recs[i], cpu);
			recs[i].time += r->time;
			recs[i].count += r->count;		
			/* Need to set these only for the first cpu */
			recs[i].name = r->name;
			recs[i].depth = r->depth;
			recs[i].id = r->id;
		}
	}
	mutex_unlock(&forp_mu);

	r = forp_snprintf_recs(recs, buf, sz);
	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
	kfree(buf);
	kfree(recs);
	return r;
}

static ssize_t forp_write_all_rec(struct file *filp, const char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	int cpu, i;

	mutex_lock(&forp_mu);
	for_each_possible_cpu(cpu)
		for (i = 0; i < FORP_REC_SIZE; i++)
			forp_reset_rec(&per_cpu(forp_recs[i], cpu));
	mutex_unlock(&forp_mu);
	return cnt;
}

static const struct file_operations forp_all_rec_ops = {
	.read 	    = forp_read_all_rec,
	.write 	    = forp_write_all_rec,
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

	if (debugfs_create_file("forp-all", 0644, d, NULL, &forp_all_rec_ops) == NULL) {
		WARN(1, "Could not create summary file\n");
		return -ENOMEM;
	}

	return 0;
}
fs_initcall(forp_init_debugfs);

static __init int forp_test(void)
{
	struct forp_rec recs[2] = {
		{ .name = "sys_open", .depth = 0 },
		{ .name = "do_sys_open", .depth = 1 },
	};
	
	forp_register(recs, 2);
	return 0;
}
late_initcall(forp_test);
