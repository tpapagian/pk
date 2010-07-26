#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/syscount.h>

struct syscount {
	struct timespec elp;
	unsigned long tot;
};

static DEFINE_PER_CPU(struct syscount, syscount[SYSCOUNT_MAX_CALLS]);

void __syscount_start(unsigned long call)
{
	if (!current)
		return;

	cputime_to_timespec(current->stime, &current->syscount);
	//getnstimeofday(&current->syscount);
	current->syscount_call = call;
	current->syscount_start = 1;
}

static inline void syscount_end_common(struct task_struct *tsk) 
{
	struct timespec ts, diff;
	struct syscount *cnt;
	unsigned long call;

	if (!tsk)
		return;

	if (tsk->syscount_start) {
		call = tsk->syscount_call;
		cputime_to_timespec(tsk->stime, &ts);
		//getnstimeofday(&ts);
		diff = timespec_sub(ts, tsk->syscount);
		cnt = get_cpu_var(syscount);
		cnt[call].elp = timespec_add_safe(cnt[call].elp, diff);
		cnt[call].tot++;
		put_cpu_var(cnt);
	}
	tsk->syscount_start = 0;
}
void syscount_end(void)
{
	syscount_end_common(current);
}

void syscount_end_task(struct task_struct *tsk)
{
	syscount_end_common(tsk);
}

void syscount_add(unsigned long call, struct timespec start, struct timespec stop)
{
	struct timespec diff;	
	struct syscount *cnt;

	diff = timespec_sub(stop, start);
	cnt = get_cpu_var(syscount);
	cnt[call].elp = timespec_add_safe(cnt[call].elp, diff);
	cnt[call].tot++;
	put_cpu_var(cnt);	
}

static int syscount_proc_show(struct seq_file *m, void *v)
{
	struct syscount *cnt;
        unsigned int c, i;
	
	for (i = 0; i < SYSCOUNT_MAX_CALLS; i++) {
		struct timespec elp = { 0, 0 };
		unsigned long tot = 0;

		for_each_possible_cpu(c) {
			cnt = &per_cpu(syscount, c)[i];
			tot += cnt->tot;
			elp = timespec_add_safe(elp, cnt->elp);
		}

		if (tot)
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
