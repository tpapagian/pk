/*
 * This is stolen from click/include/click/ewma.hh.
 */

#ifndef EWMA_H
#define EWMA_H

#include <linux/types.h>

/** @brief A DirectEWMAX with stability shift 4 (alpha 1/16), scaling factor
 *  10 (10 bits of fraction), and underlying type <code>unsigned</code>. */
#define EWMA_STABILITY 4
#define EWMA_SCALE 10

struct ewma {
	uint64_t avg;
};

void ewma_init(struct ewma *e);
uint64_t ewma_scale(struct ewma *e);
uint64_t ewma_scaled_average(struct ewma *e);
uint64_t ewma_unscaled_average(struct ewma *e);
void ewma_update(struct ewma *e, uint64_t val);

#endif
