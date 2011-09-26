/*
 * NET		Generic infrastructure for Network protocols.
 *
 * Authors:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * 		From code originally in include/net/tcp.h
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/ewma.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include <net/request_sock.h>

#ifdef CONFIG_REQUEST_SOCK_HIST
static inline void reqsk_hist_init(struct request_sock_queue *queue)
{
	queue->histogram.type = HIST_LINEAR;
	queue->histogram.start = 0;
	queue->histogram.stop = 64;
	queue->histogram.interval = 1;
	queue->histogram.buckets = _stp_stat_calc_buckets(64, 0, 1);
}
#else /* !CONFIG_REQUEST_SOCK_HIST */
static inline void reqsk_hist_init(struct request_sock_queue *queue) {
	_stp_stat_calc_buckets(64, 0, 1); // AP: TODO: HACK TO FORCE _stp_stat_calc_buckets to be included in kallsysms
}
#endif /* CONFIG_REQUEST_SOCK_HIST */

/*
 * Maximum number of SYN_RECV sockets in queue per LISTEN socket.
 * One SYN_RECV socket costs about 80bytes on a 32bit machine.
 * It would be better to replace it with a global counter for all sockets
 * but then some measure against one socket starving all other sockets
 * would be needed.
 *
 * It was 128 by default. Experiments with real servers show, that
 * it is absolutely not enough even at 100conn/sec. 256 cures most
 * of problems. This value is adjusted to 128 for very small machines
 * (<=32Mb of memory) and to 1024 on normal or better ones (>=256Mb).
 * Note : Dont forget somaxconn that may limit backlog too.
 */
int sysctl_max_syn_backlog = 256;

int reqsk_queue_alloc(struct request_sock_queue *queue,
		      struct listen_sock_table *ltable, int node)
{
	struct listen_sock *lopt;

	lopt = kzalloc_node(sizeof(struct listen_sock), GFP_KERNEL, node);
	if (lopt == NULL)
		return -ENOMEM;

	for (lopt->max_qlen_log = 3;
	     (1 << lopt->max_qlen_log) < ltable->nr_table_entries;
	     lopt->max_qlen_log++);

	reqsk_hist_init(queue);
	ewma_init(&queue->queue_len_ewma);
	ewma_init(&queue->conn_per_sec_ewma);

	rwlock_init(&queue->syn_wait_lock);
	queue->rskq_accept_head = NULL;
	lopt->table = ltable;

	write_lock_bh(&queue->syn_wait_lock);
	queue->listen_opt = lopt;
	write_unlock_bh(&queue->syn_wait_lock);

	return 0;
}

struct listen_sock_table *
reqsk_queue_table_alloc(unsigned int nr_table_entries, int node)
{
	struct listen_sock_table *ltable;
	size_t ltable_size = sizeof(struct listen_sock_table);

	// AP: TODO disable this check for multi-accept code. nr_table_entries
	// is passed in as the size of the backlog queue for one core. However,
	// since the request queue hash table is now global, the
	// nr_table_entries should be proportional to the number of cores. The
	// correct solution will be to figure out exactly what nr_table_entries
	// and sysctl_max_syn_backlog should mean.
	//
	//nr_table_entries = min_t(u32, nr_table_entries, sysctl_max_syn_backlog);
	nr_table_entries = max_t(u32, nr_table_entries, 8);
	nr_table_entries = roundup_pow_of_two(nr_table_entries + 1);
	ltable_size += nr_table_entries * sizeof(struct request_sock *);
	if (ltable_size > PAGE_SIZE)
		ltable = __vmalloc_node(ltable_size,
			GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO,
			PAGE_KERNEL, node);
	else
		ltable = kzalloc_node(ltable_size, GFP_KERNEL, node);
	if (ltable == NULL)
		return NULL;

	ltable->nr_table_entries = nr_table_entries;
	get_random_bytes(&ltable->hash_rnd, sizeof(ltable->hash_rnd));
	ltable->num_req_to_destroy = 0;
	spin_lock_init(&ltable->lock);

	return ltable;
}

//AP: TODO make sure locking is not needed here.
void __reqsk_queue_destroy(struct request_sock_queue *queue)
{
	struct listen_sock *lopt = queue->listen_opt;

	/*
	 * this is an error recovery path only
	 * no locking needed and the lopt is not NULL
	 */

	kfree(lopt);
}

static inline int get_ltable_size(struct listen_sock_table *ltable)
{
	return sizeof(struct listen_sock_table) +
		ltable->nr_table_entries * sizeof(struct request_sock *);
}

//AP: TODO make sure locking is not needed here.
static void __reqsk_queue_table_destroy(struct listen_sock_table *ltable)
{
	size_t ltable_size = get_ltable_size(ltable);

	/*
	 * this is an error recovery path only
	 * no locking needed and the lopt is not NULL
	 */

	if (ltable_size > PAGE_SIZE)
		vfree(ltable);
	else
		kfree(ltable);
}

static inline struct listen_sock *reqsk_queue_yank_listen_sk(
		struct request_sock_queue *queue)
{
	struct listen_sock *lopt;

	//AP: TODO why does the syn_wait_lock need to be held before lopt is
	//assigned?

	write_lock_bh(&queue->syn_wait_lock);
	lopt = queue->listen_opt;
	queue->listen_opt = NULL;
	write_unlock_bh(&queue->syn_wait_lock);

	return lopt;
}

void reqsk_queue_destroy(struct request_sock_queue *queue)
{
	/* make all the listen_opt local to us */
	struct listen_sock *lopt = reqsk_queue_yank_listen_sk(queue);

	//AP: XXX no need to grab the lock because reqsk_queue_yank_listen_sk
	//grabs the lock and detaches the lopt.

	//AP: TODO make sure the locking policy is correct in terms of
	//interrupt contexts.
	spin_lock_bh(&lopt->table->lock);
	lopt->table->num_req_to_destroy += lopt->qlen;
	spin_unlock_bh(&lopt->table->lock);

	kfree(lopt);
}

void reqsk_queue_table_destroy(struct listen_sock_table *ltable)
{
	size_t ltable_size = get_ltable_size(ltable);

	if (ltable->num_req_to_destroy != 0) {
		unsigned int i;
		for (i = 0; i < ltable->nr_table_entries; i++) {
			struct request_sock *req;

			while ((req = ltable->syn_table[i]) != NULL) {
				ltable->syn_table[i] = req->dl_next;
				ltable->num_req_to_destroy--;
				reqsk_free(req);
			}
		}
	}

	WARN_ON(ltable->num_req_to_destroy != 0);
	if (ltable_size > PAGE_SIZE)
		vfree(ltable);
	else
		kfree(ltable);
}
