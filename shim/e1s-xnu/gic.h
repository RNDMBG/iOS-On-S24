#pragma once
#include <stdint.h>

struct gic_info {
	uint64_t gicd;
	uint64_t gicr;
	uint64_t mpidr;
	int sre;
};

int gic_prepare(struct gic_info *out);
