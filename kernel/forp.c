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

static struct {
	struct dentry *root;
} forp_debugfs;

void forp_test(void)
{
	printk(KERN_INFO "forp_test: ...\n");
}
EXPORT_SYMBOL_GPL(forp_test);

void forp_init_task(struct task_struct *t)
{
	
}

static __init int forp_init_debugfs(void)
{
	static struct dentry *d;

	d = debugfs_create_dir("forp", NULL);
	if (!d) {
		printk(KERN_ERR "Could not create debugfs directory 'forp'\n");
		return -ENOMEM;
	}

	forp_debugfs.root = d;
	return 0;
}
fs_initcall(forp_init_debugfs);
