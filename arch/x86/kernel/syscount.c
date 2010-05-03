#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/unistd.h>

struct syscount {
	struct timespec elp;
	unsigned long tot;
};

static DEFINE_PER_CPU(struct syscount, syscount[NR_syscalls]);

void __syscount_start(unsigned long call)
{
	if (!current)
		return;

	getnstimeofday(&current->syscount);
	current->syscount_call = call;
	current->syscount_start = 1;
}

void syscount_end(void)
{
	struct timespec ts, diff;
	struct syscount *cnt;
	unsigned long call;

	if (!current)
		return;

	if (current->syscount_start) {
		call = current->syscount_call;
		getnstimeofday(&ts);
		diff = timespec_sub(ts, current->syscount);
		cnt = get_cpu_var(syscount);
		cnt[call].elp = timespec_add_safe(cnt[call].elp, diff);
		cnt[call].tot++;
		put_cpu_var(cnt);
	}
	current->syscount_start = 0;
}

static int syscount_proc_show(struct seq_file *m, void *v)
{
	struct syscount *cnt;
        unsigned int c, i;
	
	for (i = 0; i < NR_syscalls; i++) {
		struct timespec elp = { 0, 0 };
		unsigned long tot = 0;

		for_each_possible_cpu(c) {
			cnt = &per_cpu(syscount, c)[i];
			tot += cnt->tot;
			elp = timespec_add_safe(elp, cnt->elp);
		}

		seq_printf(m, "%u %lu %lu %lu\n", 
			   i, tot, elp.tv_sec, elp.tv_nsec);
	}

        return 0;
}

static int syscount_proc_open(struct inode *inode, struct file *file)
{
        return single_open(file, syscount_proc_show, NULL);
}

static const struct file_operations syscount_proc_operations = {
        .open           = syscount_proc_open,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = single_release,
};

static int __init syscount_proc_init(void)
{
        proc_create("syscount", 0, NULL, &syscount_proc_operations);
        return 0;
}
module_init(syscount_proc_init);
