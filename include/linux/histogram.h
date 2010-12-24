#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <linux/types.h>

#ifndef NEED_STAT_LOCKS
#define NEED_STAT_LOCKS 0
#endif

/* maximum buckets for a linear histogram */
#ifndef STP_MAX_BUCKETS
#define STP_MAX_BUCKETS 128
#endif

/* buckets for log histogram. */
#define HIST_LOG_BUCKETS 128
#define HIST_LOG_BUCKET0 64

/** histogram type */
enum histtype { HIST_NONE, HIST_LOG, HIST_LINEAR };

/** Statistics are stored in this struct.  This is per-cpu or per-node data 
    and is variable length due to the unknown size of the histogram. */
struct stat_data {
	int64_t count;
	int64_t sum;
	int64_t min, max;
#if NEED_STAT_LOCKS == 1
	spinlock_t lock;
#endif
	int64_t histogram[512];
};

/** Information about the histogram data collected. This data 
    is global and not duplicated per-cpu. */

struct Hist {
	enum histtype type;
	int start;
	int stop;
	int interval;
	int buckets;
};

int  _stp_stat_calc_buckets(int stop, int start, int interval);
void _stp_stat_add(struct Hist *st, struct stat_data *sd, int64_t val);
void _stp_stat_print_histogram_buf(struct Hist *st, struct stat_data *sd);

struct seq_file;
void _stp_stat_print_histogram_seq(struct Hist *st, struct stat_data *sd, struct seq_file *f);

#endif
