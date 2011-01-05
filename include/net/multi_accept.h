#ifndef _MULTI_ACCEPT_H
#define _MULTI_ACCEPT_H

#include <net/inet_connection_sock.h>

extern int sysctl_multi_accept_lb;
extern int sysctl_multi_accept_debug;
extern int sysctl_multi_accept_c;

struct ma_per_cpu {
	int 			mapc_cpu;
	struct sock		*mapc_sk;
	struct timer_list 	mapc_timer;
	void 			*mapc_priv;
};

struct multi_accept {
	struct ma_per_cpu 	ma_pc[NR_CPUS];
	struct timer_list       ma_timer;
	void 			*ma_priv;
};

struct multi_accept_lb_ops {
	void		(*balance) (struct sock *);
	int		(*steal) (struct sock *);
	unsigned long	(*handler) (struct sock *);
	unsigned long	(*local_handler) (struct ma_per_cpu *);

	void		(*print) (struct sock *, int, struct seq_file *);
	void		(*reset) (struct sock *, int);
};

int ma_init(struct sock *sk);

void ma_start_timer(struct sock *sk);
void ma_stop_timer(struct sock *sk);
void ma_start_per_cpu_timer(struct sock *sk, int cpu);
void ma_stop_per_cpu_timer(struct sock *sk, int cpu);

void ma_lb_register(struct multi_accept_lb_ops *ops);
void ma_lb_unregister(void);

int  ma_lb_steal(struct sock *sk);
void ma_lb_balance(struct sock *sk);
void ma_lb_print(struct sock *sk, int, struct seq_file *f);
void ma_lb_reset(struct sock *sk, int);

static inline struct multi_accept *ma_sk(struct sock *sk)
{
	return inet_csk(sk)->icsk_ma;
}

static inline struct sock *ma_get_sk(struct sock *sk, int cpu)
{
	return ma_sk(sk)->ma_pc[cpu].mapc_sk;
}

static inline struct sock *ma_get_local_or_steal_sk(struct sock *sk)
{
	if (ma_sk(sk))
		return ma_get_sk(sk, ma_lb_steal(sk));
	else
		return sk;
}

static inline struct sock *ma_get_local_sk(struct sock *sk)
{
	if (ma_sk(sk))
		return ma_get_sk(sk, smp_processor_id());
	else 
		return sk;
}

#endif /* _MULTI_ACCEPT_H */
