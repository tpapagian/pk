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
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "forp.h"
#include "forp-entry-label.h"

static inline struct forp_label *forp_get_label(int i)
{
	static struct forp_label no_label = { "---", 0 };
	
	if (i >= FORP_DYN_REC_SIZE)
		return &forp_entry_label[i - FORP_DYN_REC_SIZE];
	if (i >= forp_dyn_label_num)
		return &no_label;
	return &forp_dyn_labels[i];
}

static int forp_open_rec(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static int forp_snprintf_recs(struct forp_rec *recs, char *buf, int sz)
{
	char *p, *e;
	int i;

	p = buf;
	e = p + sz;

	p += snprintf(p, e - p, "# Function                          "
		      "Depth         Hit         Sched   IRQ     Time\n");

	for (i = 0; i < FORP_REC_SIZE; i++) {
		struct forp_rec *rec = &recs[i];
		if (rec->count) {
			struct forp_label *label = forp_get_label(i);
			p += snprintf(p, e - p, 
				      "  %-30.30s      %3u  %10llu    %10llu    %10llu    %-10llu\n",
				      label->name, label->depth, rec->count, 
				      rec->sched, rec->irq, rec->time);
		}
	}

	return p - buf;
}

static ssize_t forp_read_rec(struct file *filp, char __user *ubuf,
			     size_t cnt, loff_t *ppos)
{
	unsigned long cpu = (unsigned long)filp->private_data;
	char *buf;
	int r;
	/* About 256 characters per line */
	int sz = FORP_REC_SIZE * 256;

	buf = kzalloc(sz, GFP_KERNEL);
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

static ssize_t forp_read_aggregate(char __user *ubuf, size_t cnt, 
				   loff_t *ppos, struct forp_rec __percpu *rec)
{
	/* About 256 characters per line */
	int sz = 256 + (FORP_REC_SIZE * 256);
	struct forp_rec *recs;
	int i, cpu, r;
	char *buf;

	recs = kzalloc(FORP_REC_SIZE * sizeof(struct forp_rec), GFP_KERNEL);
	if (recs == NULL)
		return -ENOMEM;

	buf = kzalloc(sz, GFP_KERNEL);
	if (buf == NULL) {
		kfree(recs);
		return -ENOMEM;
	}

	mutex_lock(&forp_mu);

	for (i = 0; i < FORP_REC_SIZE; i++) {
		for_each_possible_cpu(cpu) {
			struct forp_rec *r = &per_cpu(rec[i], cpu);
			recs[i].time += r->time;
			recs[i].count += r->count;		
			recs[i].sched += r->sched;
			recs[i].irq += r->irq;
		}
	}
	mutex_unlock(&forp_mu);

	r = forp_snprintf_recs(recs, buf, sz);
	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
	kfree(buf);
	kfree(recs);
	return r;
}

static void forp_write_aggregate(struct forp_rec __percpu *rec)
{
	int cpu, i;

	mutex_lock(&forp_mu);
	for_each_possible_cpu(cpu)
		for (i = 0; i < FORP_REC_SIZE; i++)
			forp_reset_rec(&per_cpu(rec[i], cpu));
	mutex_unlock(&forp_mu);
}

static ssize_t forp_read_all_rec(struct file *filp, char __user *ubuf,
				 size_t cnt, loff_t *ppos)
{
	return forp_read_aggregate(ubuf, cnt, ppos, forp_recs);
}

static ssize_t forp_write_all_rec(struct file *filp, const char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	forp_write_aggregate(forp_recs);
	return cnt;
}

static ssize_t
forp_labels_read(struct file *filp, char __user *ubuf,
		 size_t cnt, loff_t *ppos)
{
	unsigned long sz;
	char *buf, *p;
	int i, r = 0;
	
	/* Roughly (log10(MAX_ULONG) + ':' + 32) * forp_rec_num */
	sz = (20 + sizeof(forp_dyn_labels[0].name)) * forp_dyn_label_num;
	if ((buf = kmalloc(sz, GFP_KERNEL)) == NULL)
		return -ENOMEM;
	
	p = buf;
	mutex_lock(&forp_mu);
	for (i = 0; i < forp_dyn_label_num; i++) {
		r += snprintf(p, sz - r, "%u:%s ", 
			      forp_dyn_labels[i].depth, 
			      forp_dyn_labels[i].name);
		p = &buf[r];
	}
	mutex_unlock(&forp_mu);
	
	if (r)
		buf[r - 1] = '\n';
	
	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
	kfree(buf);
	return r;
}

static ssize_t
forp_labels_write(struct file *filp, const char __user *ubuf,
                 size_t cnt, loff_t *ppos)
{
        struct forp_label *labels;
        unsigned int count, num = 0;
        char *p, *buf;
        int ret = -EINVAL;

        if ((buf = kmalloc(cnt + 1, GFP_KERNEL)) == NULL)
                return -ENOMEM;

        labels = kzalloc(sizeof(*labels) * FORP_REC_SIZE, GFP_KERNEL);
        if (labels == NULL) {
                kfree(buf);
                return -ENOMEM;
        }

        if (copy_from_user(buf, ubuf, cnt)) {
                ret = -EFAULT;
                goto done;
        }
        buf[cnt] = 0;

        p = buf;
        while (*p) {
                if (num == FORP_REC_SIZE)
                        goto done;

                if (sscanf(p, "%u:%32s%n", &labels[num].depth, 
			   labels[num].name, &count) != 2)
                        goto done;

                num++;
                p += count;

                if (*p != ';' && *p != ',' && *p != ' ')
                        break;
                p++;
        }

	forp_register(labels, num);

        *ppos += cnt;
        ret = cnt;

done:
	/* forp.c will eventually free labels */
        kfree(buf);
        return ret;
}

static ssize_t
forp_enable_read(struct file *filp, char __user *ubuf,
           size_t cnt, loff_t *ppos)
{
        char buf[64];           /* big enough to hold a number */
        int r;

        r = sprintf(buf, "%u\n", forp_enable);
        return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
forp_enable_write(struct file *filp, const char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	unsigned long val;
	char buf[64];           /* big enough to hold a number */
	int ret;
	
	if (cnt >= sizeof(buf))
		return -EINVAL;
	
	if (copy_from_user(&buf, ubuf, cnt))
	    return -EFAULT;
    
	buf[cnt] = 0;
	
	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	if (val > FORP_ENABLE_ALL)
		return -EINVAL;
	
	mutex_lock(&forp_mu);
	if (forp_enable ^ val) {
		if (val) {
			if (forp_enable)
				forp_deinit();

			ret = forp_init(val);
			if (ret)
				goto out;
		} else {
			forp_deinit();
		}
	}

	*ppos += cnt;
	ret = cnt;
out:
	mutex_unlock(&forp_mu);
	return ret;
}

#if 0
static ssize_t forp_read_entry_rec(struct file *filp, char __user *ubuf,
				   size_t cnt, loff_t *ppos)
{
	return forp_read_aggregate(ubuf, cnt, ppos, FORP_ENTRY_REC_SIZE, 
				   forp_entry_recs, forp_entry_label);
}

static ssize_t forp_write_entry_rec(struct file *filp, const char __user *ubuf,
				    size_t cnt, loff_t *ppos)
{
	forp_write_aggregate(FORP_ENTRY_REC_SIZE, forp_entry_recs);
	return cnt;
}
#endif

static ssize_t
forp_flags_read(struct file *filp, char __user *ubuf,
		 size_t cnt, loff_t *ppos)
{
        char buf[64];           /* big enough to hold a number */
        int r;

        r = sprintf(buf, "%lx\n", forp_flags);
        return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
forp_flags_write(struct file *filp, const char __user *ubuf,
                 size_t cnt, loff_t *ppos)
{
	unsigned long val;
	char buf[64];           /* big enough to hold a number */
	int ret;
	
	if (cnt >= sizeof(buf))
		return -EINVAL;
	
	if (copy_from_user(&buf, ubuf, cnt))
	    return -EFAULT;
    
	buf[cnt] = 0;
	
	ret = strict_strtoul(buf, 16, &val);
	if (ret < 0)
		return ret;

	if (val > FORP_FLAG_ALL)
		return -EINVAL;
	
	mutex_lock(&forp_mu);
	forp_flags = val;
	mutex_unlock(&forp_mu);
	return cnt;
}

#define F_OPS(readop, writeop) \
{		      	       \
	.read = readop,        \
	.write = writeop       \
}	       	 	       

static struct {
	const char *name;
	const struct file_operations ops;
} forp_debugfs[] = {
	{ "forp-all",	  F_OPS(forp_read_all_rec, forp_write_all_rec) },
	{ "forp-labels",  F_OPS(forp_labels_read, forp_labels_write) },
	/* XXX forp-conf is deprecated, use forp-labels */
	{ "forp-conf",	  F_OPS(forp_labels_read, forp_labels_write) },
	{ "forp-flags",	  F_OPS(forp_flags_read, forp_flags_write) },
	{ "forp-enable",  F_OPS(forp_enable_read, forp_enable_write) },
#if 0
	{ "forp-entry",   F_OPS(forp_read_entry_rec, forp_write_entry_rec) },
#endif
	{ NULL }
};

static __init int forp_init_debugfs(void)
{
	unsigned long cpu, i;
	struct dentry *d;
	char *name;

	d = debugfs_create_dir("forp", NULL);
	if (!d) {
		printk(KERN_ERR "Could not create debugfs directory 'forp'\n");
		return -ENOMEM;
	}

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

	for (i = 0; forp_debugfs[i].name != NULL; i++) {
		if (debugfs_create_file(forp_debugfs[i].name, 0644, d, NULL, 
					&forp_debugfs[i].ops) == NULL)
		{
			WARN(1, "Could not create %s file\n", forp_debugfs[i].name);
			return -ENOMEM;
		}
	}

	return 0;
}
fs_initcall(forp_init_debugfs);
