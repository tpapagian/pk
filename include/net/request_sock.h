/*
 * NET		Generic infrastructure for Network protocols.
 *
 *		Definitions for request_sock 
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
#ifndef _REQUEST_SOCK_H
#define _REQUEST_SOCK_H

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/histogram.h>
#include <linux/ewma.h>

#include <net/sock.h>

struct request_sock;
struct sk_buff;
struct dst_entry;
struct proto;

/* empty to "strongly type" an otherwise void parameter.
 */
struct request_values {
};

struct request_sock_ops {
	int		family;
	int		obj_size;
	struct kmem_cache	*slab;
	char		*slab_name;
	int		(*rtx_syn_ack)(struct sock *sk,
				       struct request_sock *req,
				       struct request_values *rvp);
	void		(*send_ack)(struct sock *sk, struct sk_buff *skb,
				    struct request_sock *req);
	void		(*send_reset)(struct sock *sk,
				      struct sk_buff *skb);
	void		(*destructor)(struct request_sock *req);
	void		(*syn_ack_timeout)(struct sock *sk,
					   struct request_sock *req);
};

/* struct request_sock - mini sock to represent a connection request
 */
struct request_sock {
	struct request_sock		*dl_next; /* Must be first member! */
	u16				mss;
	u8				retrans;
	u8				cookie_ts; /* syncookie: encode tcpopts in timestamp */
	/* The following two fields can be easily recomputed I think -AK */
	u32				window_clamp; /* window clamp at creation time */
	u32				rcv_wnd;	  /* rcv_wnd offered first time */
	u32				ts_recent;
	unsigned long			expires;
	const struct request_sock_ops	*rsk_ops;
	struct sock			*sk;
	u32				secid;
	u32				peer_secid;
};

static inline struct request_sock *reqsk_alloc(const struct request_sock_ops *ops)
{
	struct request_sock *req = kmem_cache_alloc(ops->slab, GFP_ATOMIC);

	if (req != NULL)
		req->rsk_ops = ops;

	return req;
}

static inline void __reqsk_free(struct request_sock *req)
{
	kmem_cache_free(req->rsk_ops->slab, req);
}

static inline void reqsk_free(struct request_sock *req)
{
	req->rsk_ops->destructor(req);
	__reqsk_free(req);
}

extern int sysctl_max_syn_backlog;

/** struct listen_sock - listen state
 *
 * @max_qlen_log - log_2 of maximal queued SYNs/REQUESTs
 */
struct listen_sock {
	u8			max_qlen_log;
	/* 3 bytes hole, try to use */
	int			qlen;
	int			qlen_young;
	struct listen_sock_table* table;
};

struct listen_sock_table {
	// Read mostly
	u32			nr_table_entries; 
	u32			hash_rnd;
	u32			num_req_to_destroy;

	// Read/Wrtie
	spinlock_t		lock ____cacheline_aligned_in_smp;
	int			clock_hand; // AP: XXX TODO not sure wether this should go here or in listen_sock
	struct request_sock	*syn_table[0];
};

/** struct request_sock_queue - queue of request_socks
 *
 * @rskq_accept_head - FIFO head of established children
 * @rskq_accept_tail - FIFO tail of established children
 * @rskq_defer_accept - User waits for some data after accept()
 * @syn_wait_lock - serializer
 *
 * %syn_wait_lock is necessary only to avoid proc interface having to grab the main
 * lock sock while browsing the listening hash (otherwise it's deadlock prone).
 *
 * This lock is acquired in read mode only from listening_get_next() seq_file
 * op and it's acquired in write mode _only_ from code that is actively
 * changing rskq_accept_head. All readers that are holding the master sock lock
 * don't need to grab this lock in read mode too as rskq_accept_head. writes
 * are always protected from the main sock lock.
 */
struct request_sock_queue {
	struct request_sock	*rskq_accept_head;
	struct request_sock	*rskq_accept_tail;
	rwlock_t		syn_wait_lock; // AP: XXX TODO This lock is now
					       // useless because it does not
					       // protect the ltable anymore
					       // and there are many of these
					       // locks. MUST FIX.
	u8			rskq_defer_accept;
	/* 3 bytes hole, try to pack */
	struct listen_sock	*listen_opt;

#ifdef CONFIG_REQUEST_SOCK_HIST
	struct Hist		histogram;
	struct stat_data	stats;
#endif

	struct ewma		queue_len_ewma;
	struct ewma		conn_per_sec_ewma;
};

extern int reqsk_queue_alloc(struct request_sock_queue *queue,
		             struct listen_sock_table *ltable, int node);
extern struct listen_sock_table *
reqsk_queue_table_alloc(unsigned int nr_table_entries, int node);
extern void reqsk_queue_table_destroy(struct listen_sock_table *ltable);

extern void __reqsk_queue_destroy(struct request_sock_queue *queue);
extern void reqsk_queue_destroy(struct request_sock_queue *queue);

static inline struct request_sock *
	reqsk_queue_yank_acceptq(struct request_sock_queue *queue)
{
	struct request_sock *req = queue->rskq_accept_head;

	queue->rskq_accept_head = NULL;
	return req;
}

static inline int reqsk_queue_empty(struct request_sock_queue *queue)
{
	return queue->rskq_accept_head == NULL;
}

static inline void reqsk_queue_unlink(struct request_sock_queue *queue,
				      struct request_sock *req,
				      struct request_sock **prev_req)
{
	write_lock(&queue->syn_wait_lock);
	*prev_req = req->dl_next;
	write_unlock(&queue->syn_wait_lock);
}

static inline void reqsk_hist_update(struct request_sock_queue *queue);
static inline int reqsk_queue_len(const struct request_sock_queue *queue);
extern void ma_lb_add_queue(struct sock *sk, int);

static inline void reqsk_queue_add(struct request_sock_queue *queue,
				   struct request_sock *req,
				   struct sock *parent,
				   struct sock *child)
{
	reqsk_hist_update(queue);
	ma_lb_add_queue(parent, parent->sk_ack_backlog);

	req->sk = child;
	sk_acceptq_added(parent);

	if (queue->rskq_accept_head == NULL)
		queue->rskq_accept_head = req;
	else
		queue->rskq_accept_tail->dl_next = req;

	queue->rskq_accept_tail = req;
	req->dl_next = NULL;
}

static inline struct request_sock *reqsk_queue_remove(struct request_sock_queue *queue)
{
	struct request_sock *req = queue->rskq_accept_head;

	WARN_ON(req == NULL);

	queue->rskq_accept_head = req->dl_next;
	if (queue->rskq_accept_head == NULL)
		queue->rskq_accept_tail = NULL;

	return req;
}

static inline struct sock *reqsk_queue_get_child(struct request_sock_queue *queue,
						 struct sock *parent)
{
	struct request_sock *req = reqsk_queue_remove(queue);
	struct sock *child = req->sk;

	WARN_ON(child == NULL);

	sk_acceptq_removed(parent);
	__reqsk_free(req);
	return child;
}

static inline int reqsk_queue_removed(struct request_sock_queue *queue,
				      struct request_sock *req)
{
	struct listen_sock *lopt = queue->listen_opt;

	if (req->retrans == 0)
		--lopt->qlen_young;

	return --lopt->qlen;
}

static inline int reqsk_queue_added(struct request_sock_queue *queue)
{
	struct listen_sock *lopt = queue->listen_opt;
	const int prev_qlen = lopt->qlen;

	lopt->qlen_young++;
	lopt->qlen++;
	return prev_qlen;
}

static inline int reqsk_queue_len(const struct request_sock_queue *queue)
{
	return queue->listen_opt != NULL ? queue->listen_opt->qlen : 0;
}

static inline int reqsk_queue_len_young(const struct request_sock_queue *queue)
{
	return queue->listen_opt->qlen_young;
}

static inline int reqsk_queue_is_full(const struct request_sock_queue *queue)
{
	return queue->listen_opt->qlen >> queue->listen_opt->max_qlen_log;
}

static inline void reqsk_queue_hash_req(struct request_sock_queue *queue,
					u32 hash, struct request_sock *req,
					unsigned long timeout)
{
	struct listen_sock_table *ltable = queue->listen_opt->table;

	req->expires = jiffies + timeout;
	req->retrans = 0;
	req->sk = NULL;

	spin_lock(&ltable->lock);

	req->dl_next = ltable->syn_table[hash];

	write_lock(&queue->syn_wait_lock);
	ltable->syn_table[hash] = req;
	write_unlock(&queue->syn_wait_lock);

	spin_unlock(&ltable->lock);
}

#ifdef CONFIG_REQUEST_SOCK_HIST
static inline void reqsk_hist_update(struct request_sock_queue *queue)
{
	int len = reqsk_queue_len(queue);
	_stp_stat_add(&queue->histogram, &queue->stats, len);
}

static inline void reqsk_hist_clear(struct request_sock_queue *queue)
{
	memset(&queue->stats, 0, sizeof(struct stat_data));
}

static inline void reqsk_queue_hash_print(struct request_sock_queue *queue, struct seq_file *f)
{
	_stp_stat_print_histogram_seq(&queue->histogram, &queue->stats, f);
}
#else /* !CONFIG_REQUEST_SOCK_HIST */
static inline void reqsk_hist_update(struct request_sock_queue *queue) {}
static inline void reqsk_hist_clear(struct request_sock_queue *queue) {}
static inline void reqsk_queue_hash_print(struct request_sock_queue *queue, struct seq_file *f) {}
#endif /* CONFIG_REQUEST_SOCK_HIST */

#endif /* _REQUEST_SOCK_H */
