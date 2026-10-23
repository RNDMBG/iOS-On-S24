#pragma once
#include <stdint.h>

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);
