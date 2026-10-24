#ifndef E1S_FDT_H
#define E1S_FDT_H

#include <stdint.h>

struct fdt_range {
	uint64_t base, size;
};

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);
int fdt_find_framebuffer(uintptr_t fdt, uint64_t *base, uint64_t *size,
    uint32_t *width, uint32_t *height, uint32_t *stride);
int fdt_memory_ranges(uintptr_t fdt, struct fdt_range *out, int max);
int fdt_reserved_ranges(uintptr_t fdt, struct fdt_range *out, int max);

#endif
