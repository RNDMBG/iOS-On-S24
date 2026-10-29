#pragma once
#include <stdint.h>

int adt_set_boot_cpu_mpidr(uintptr_t adt, uint64_t size, uint32_t mpidr);
int adt_set_dram(uintptr_t adt, uint64_t size, uint64_t base, uint64_t mem_size);
int adt_set_pram(uintptr_t adt, uint64_t size, uint64_t base, uint64_t pram_size);
int adt_rebase_ramdisk(uintptr_t adt, uint64_t size, int64_t delta);
