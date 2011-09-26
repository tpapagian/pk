#include <net/inet_connection_sock.h>
#include <net/multi_accept.h>

static unsigned long ma_timer_expires = HZ/10;
static struct multi_accept_lb_ops *ma_ops = NULL;

int sysctl_multi_accept_lb __read_mostly = 1;
EXPORT_SYMBOL_GPL(sysctl_multi_accept_lb);

int sysctl_multi_accept_debug __read_mostly = 0;
EXPORT_SYMBOL_GPL(sysctl_multi_accept_debug);

int sysctl_multi_accept_c __read_mostly = 0;
EXPORT_SYMBOL_GPL(sysctl_multi_accept_c);

int sysctl_multi_accept_private_ltable __read_mostly = 0;
EXPORT_SYMBOL_GPL(sysctl_multi_accept_private_ltable);

int ma_init(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	icsk->icsk_ma = kzalloc(sizeof(struct multi_accept), GFP_KERNEL);
	if (!icsk->icsk_ma)
		return -ENOMEM;

	return 0;
}

void ma_lb_register(struct multi_accept_lb_ops *ops)
{
	ma_ops = ops;
}
EXPORT_SYMBOL_GPL(ma_lb_register);

void ma_lb_unregister(void)
{
	ma_ops = NULL;
	rcu_barrier();
}
EXPORT_SYMBOL_GPL(ma_lb_unregister);

void ma_lb_balance(struct sock *sk)
{
	rcu_read_lock();
	if (ma_ops) ma_ops->balance(sk);
	rcu_read_unlock();
}

int ma_lb_accept(struct sock *sk, int flags, int *err, struct sock **newsk)
{
	int r = -EINVAL;

	rcu_read_lock();
	if (ma_ops) {
		*newsk = ma_ops->accept(sk, flags, err);
		r = 0;
	}
	rcu_read_unlock();

	return r;
}

void ma_lb_add_queue(struct sock *sk, int len)
{
	if (!inet_csk(sk)->icsk_ma)
		return;

	rcu_read_lock();
	if (ma_ops) ma_ops->add_queue(sk, len);
	rcu_read_unlock();
}

void ma_lb_overflow(struct sock *sk)
{
	if (!inet_csk(sk)->icsk_ma)
		return;

	rcu_read_lock();
	if (ma_ops && ma_ops->overflow)
		ma_ops->overflow(sk);
	rcu_read_unlock();
}


void ma_lb_print(struct sock *sk, int cpu, struct seq_file *f)
{
	rcu_read_lock();
	if (ma_ops) ma_ops->print(sk, cpu, f);
	rcu_read_unlock();
}

void ma_lb_reset(struct sock *sk, int cpu)
{
	rcu_read_lock();
	if (ma_ops) ma_ops->reset(sk, cpu);
	rcu_read_unlock();
}

int ma_lb_listen_poll(const struct sock *sk, unsigned int *ret)
{
	int r = -1;
	rcu_read_lock();
	if (ma_ops && ma_ops->poll) {
		*ret = ma_ops->poll(sk);
		r = 0;
	}
	rcu_read_unlock();
	return r;
}

int ma_lb_data_ready(struct sock *sk, int bytes)
{
	int r = -1;
	rcu_read_lock();
	if (ma_ops && ma_ops->data_ready) {
		ma_ops->data_ready(sk, bytes);
		r = 0;
	}
	rcu_read_unlock();
	return r;
}

static void ma_handler(unsigned long a)
{
	struct sock *sk = (struct sock *) a;
	struct inet_connection_sock *icsk = inet_csk(sk);
	unsigned long expires = ma_timer_expires;

	rcu_read_lock();
	if (ma_ops) {
		unsigned long e = ma_ops->handler(sk);
		if (e != 0) expires = e;
	}
	rcu_read_unlock();

	mod_timer(&icsk->icsk_ma->ma_timer, jiffies + expires);
}

static void ma_per_cpu_handler(unsigned long a)
{
	struct ma_per_cpu *ma_pc = (struct ma_per_cpu *) a;
	unsigned long expires = ma_timer_expires;

	rcu_read_lock();
	if (ma_ops) {
		unsigned long e = ma_ops->local_handler(ma_pc);
		if (e != 0) expires = e;
	}
	rcu_read_unlock();

	mod_timer(&ma_pc->mapc_timer, jiffies + expires);
}

void ma_start_timer(struct sock *sk)
{
	struct multi_accept *ma = ma_sk(sk);
	setup_timer(&ma->ma_timer, ma_handler, (unsigned long)sk);
	mod_timer(&ma->ma_timer, jiffies + ma_timer_expires);
}

void ma_stop_timer(struct sock *sk)
{
	struct multi_accept *ma = ma_sk(sk);
	if (timer_pending(&ma->ma_timer)) 
		del_timer(&ma->ma_timer);
}

void ma_start_per_cpu_timer(struct sock *sk, int cpu)
{
	struct ma_per_cpu *ma_pc = &ma_sk(sk)->ma_pc[cpu];
	struct timer_list *timer = &ma_pc->mapc_timer;
	setup_timer(timer, ma_per_cpu_handler, (unsigned long)ma_pc);
	mod_timer(timer, jiffies + ma_timer_expires);
}

void ma_stop_per_cpu_timer(struct sock *sk, int cpu)
{
	struct timer_list *timer = &ma_sk(sk)->ma_pc[cpu].mapc_timer;
	if (timer_pending(timer))
		del_timer(timer);
}
