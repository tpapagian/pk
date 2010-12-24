#include <linux/ewma.h>

static inline uint64_t ewma_stability_shift(void)
{
	return EWMA_STABILITY;
}

static inline uint64_t ewma_compensation(void)
{
	return 1 << (EWMA_STABILITY - 1);
}

inline uint64_t ewma_scale(struct ewma *e)
{
	return EWMA_SCALE;
}

inline void ewma_init(struct ewma *e) {
	e->avg = 0;
}

/* Return the current scaled moving average.
 * The returned value has scale() bits of fraction.
 */ 
inline uint64_t ewma_scaled_average(struct ewma *e) {
	return e->avg;
}

/* Return the current moving average.
 * The returned value is unscaled.
 */
inline uint64_t ewma_unscaled_average(struct ewma *e) {
	return (e->avg + ewma_compensation()) >> ewma_scale(e);
}

inline void ewma_update(struct ewma *e, uint64_t val) {
    uint64_t val_scaled = (val << ewma_scale(e)) + ewma_compensation();
    unsigned stability = ewma_stability_shift();
    if (val_scaled < e->avg)
	e->avg -= (e->avg - val_scaled) >> stability;
    else
	e->avg += (val_scaled - e->avg) >> stability;
}
