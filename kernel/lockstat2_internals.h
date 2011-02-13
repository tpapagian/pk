extern int lockstat_lock_acquire(struct lockdep_map *lock, unsigned int subclass,
			  int trylock, int read, int hardirqs_off,
			  struct lockdep_map *nest_lock, unsigned long ip,
			  int references);

extern void lockstat_lock_release(struct lockdep_map *lock, int nested, unsigned long ip);

extern void lockstat_lock_contended(struct lockdep_map *lock, unsigned long ip);

extern void lockstat_lock_acquired(struct lockdep_map *lock, unsigned long ip);
