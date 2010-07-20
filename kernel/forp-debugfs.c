#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "forp.h"

static int forp_open_rec(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static int forp_snprintf_recs(struct forp_rec *recs, int n, char *buf, int sz)
{
	char *p, *e;
	int i;

	p = buf;
	e = p + sz;

	p += snprintf(p, e - p, "# Function                               "
		      "Hit    Time\n");

	for (i = 0; i < n; i++) {
		struct forp_rec *rec = &recs[i];
		if (rec->count)
			p += snprintf(p, e - p, 
				      "  %-30.30s  %10llu    %10llu\n",
				      rec->name, rec->count, rec->time);
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
	r = forp_snprintf_recs(&per_cpu(forp_recs[0], cpu), forp_rec_num,
			       buf, sz);
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
	/* About 256 characters per line */
	int sz = FORP_REC_SIZE * 256;
	struct forp_rec *recs;
	int i, cpu, r;
	char *buf;

	recs = kzalloc(forp_rec_num * sizeof(struct forp_rec), GFP_KERNEL);
	if (recs == NULL)
		return -ENOMEM;

	buf = kzalloc(sz, GFP_KERNEL);
	if (buf == NULL) {
		kfree(recs);
		return -ENOMEM;
	}

	mutex_lock(&forp_mu);

	for (i = 0; i < forp_rec_num; i++) {
		for_each_possible_cpu(cpu) {
			struct forp_rec *r = &per_cpu(forp_recs[i], cpu);
			recs[i].time += r->time;
			recs[i].count += r->count;		
			/* Need to set these only for the first cpu */
			strcpy(recs[i].name, r->name);
			recs[i].depth = r->depth;
			recs[i].id = r->id;
		}
	}
	mutex_unlock(&forp_mu);

	r = forp_snprintf_recs(recs, forp_rec_num, buf, sz);
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
		for (i = 0; i < forp_rec_num; i++)
			forp_reset_rec(&per_cpu(forp_recs[i], cpu));
	mutex_unlock(&forp_mu);
	return cnt;
}

static const struct file_operations forp_all_rec_ops = {
	.read 	    = forp_read_all_rec,
	.write 	    = forp_write_all_rec,
};

static ssize_t
forp_config_read(struct file *filp, char __user *ubuf,
		 size_t cnt, loff_t *ppos)
{
	struct forp_rec *rec;
	unsigned long sz;
	char *buf, *p;
	int i, r = 0;
	
	rec = &per_cpu(forp_recs[0], 0);
	
	/* Roughly (log10(MAX_ULONG) + ':' + 32) * forp_rec_num */
	sz = (20 + sizeof(rec->name)) * forp_rec_num;
	if ((buf = kmalloc(sz, GFP_KERNEL)) == NULL)
		return -ENOMEM;
	
	p = buf;
	mutex_lock(&forp_mu);
	for (i = 0; i < forp_rec_num; i++) {
		r += snprintf(p, sz - r, "%u:%s ", rec[i].depth, rec[i].name);
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
forp_config_write(struct file *filp, const char __user *ubuf,
                 size_t cnt, loff_t *ppos)
{
        struct forp_rec *recs;
        unsigned int count, num = 0;
        char *p, *buf;
        int ret = -EINVAL;

        if ((buf = kmalloc(cnt + 1, GFP_KERNEL)) == NULL)
                return -ENOMEM;

        recs = kzalloc(sizeof(*recs) * FORP_REC_SIZE, GFP_KERNEL);
        if (recs == NULL) {
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

                if (sscanf(p, "%u:%32s%n", &recs[num].depth, 
			   recs[num].name, &count) != 2)
                        goto done;

                num++;
                p += count;

                if (*p != ';' && *p != ',' && *p != ' ')
                        break;
                p++;
        }

	forp_register(recs, num);

        *ppos += cnt;
        ret = cnt;

done:
        kfree(recs);
        kfree(buf);
        return ret;
}

static const struct file_operations forp_config_ops = {
	.read           = forp_config_read,
	.write          = forp_config_write,
};

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
	
	val = !!val;

	mutex_lock(&forp_mu);
	if (forp_enable ^ val) {
		if (val) {
			ret = forp_init();
			if (ret)
				goto out;
		} else {
			forp_deinit();
		}
	}
out:
	mutex_unlock(&forp_mu);
	*ppos += cnt;
	return cnt;
}

static const struct file_operations forp_enable_ops = {
	.read           = forp_enable_read,
	.write          = forp_enable_write,
};

static ssize_t forp_read_entry_rec(struct file *filp, char __user *ubuf,
				   size_t cnt, loff_t *ppos)
{
	/* About 256 characters per line */
	int sz = FORP_ENTRY_REC_SIZE * 256;
	struct forp_rec *recs;
	int r, i, cpu;
	char *buf;
	
	recs = kzalloc(FORP_ENTRY_REC_SIZE * sizeof(struct forp_rec), GFP_KERNEL);
	if (recs == NULL)
		return -ENOMEM;

	buf = kzalloc(sz, GFP_KERNEL);
	if (buf == NULL) {
		kfree(recs);
		return -ENOMEM;
	}	

	mutex_lock(&forp_mu);

	for (i = 0; i < FORP_ENTRY_REC_SIZE; i++) {
		for_each_possible_cpu(cpu) {
			struct forp_rec *r = &per_cpu(forp_entry_recs[i], cpu);
			recs[i].time += r->time;
			recs[i].count += r->count;		
			/* Need to set these only for the first cpu */
			strcpy(recs[i].name, "foo");
		}
	}
	mutex_unlock(&forp_mu);

	r = forp_snprintf_recs(recs, FORP_ENTRY_REC_SIZE, buf, sz);
	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
	kfree(buf);
	kfree(recs);
	return r;
}

static ssize_t forp_write_entry_rec(struct file *filp, const char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	int cpu, i;

	mutex_lock(&forp_mu);
	for_each_possible_cpu(cpu)
		for (i = 0; i < FORP_ENTRY_REC_SIZE; i++)
			forp_reset_rec(&per_cpu(forp_entry_recs[i], cpu));
	mutex_unlock(&forp_mu);
	return cnt;
}

static const struct file_operations forp_entry_rec_ops = {
	.read           = forp_read_entry_rec,
	.write          = forp_write_entry_rec,
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

	if (debugfs_create_file("forp-conf", 0644, d, NULL, &forp_config_ops) == NULL) {
		WARN(1, "Could not create config file\n");
		return -ENOMEM;
	}

	if (debugfs_create_file("forp-enable", 0644, d, NULL, &forp_enable_ops) == NULL) {
		WARN(1, "Could not create enable file\n");
		return -ENOMEM;
	}

	if (debugfs_create_file("forp-entry", 0644, d, NULL, &forp_entry_rec_ops) == NULL) {
		WARN(1, "Could not create entry summary file\n");
		return -ENOMEM;
	}

	return 0;
}
fs_initcall(forp_init_debugfs);
