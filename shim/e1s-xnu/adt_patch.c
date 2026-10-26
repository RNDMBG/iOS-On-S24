#include "adt_patch.h"

#define ADT_MAX_DEPTH 8

static int in_bounds(uint64_t size, uint64_t off, uint64_t len)
{
	return off <= size && len <= size - off;
}

static int rd32(uintptr_t base, uint64_t size, uint32_t off, uint32_t *v)
{
	if (!in_bounds(size, off, 4))
		return -1;
	*v = *(const volatile uint32_t *)(base + off);
	return 0;
}

static int prop_name_is(uintptr_t base, uint32_t off, const char *want)
{
	const volatile unsigned char *p = (const volatile unsigned char *)(base + off);
	for (int i = 0; i < 32; i++) {
		if (!want[i])
			return p[i] == 0;
		if (p[i] != (unsigned char)want[i])
			return 0;
	}
	return 0;
}

static int skip_props(uintptr_t base, uint64_t size, uint32_t *off, uint32_t nprops)
{
	for (uint32_t i = 0; i < nprops; i++) {
		uint32_t len;
		if (!in_bounds(size, *off, 36) ||
		    rd32(base, size, *off + 32, &len))
			return -1;
		len &= 0x7fffffffU;
		uint64_t next = (uint64_t)*off + 36 + ((len + 3U) & ~3U);
		if (next > size)
			return -1;
		*off = (uint32_t)next;
	}
	return 0;
}

static int node_end(uintptr_t base, uint64_t size, uint32_t off,
    uint32_t *end, int depth)
{
	uint32_t nprops, nchildren;
	if (depth > ADT_MAX_DEPTH ||
	    rd32(base, size, off, &nprops) ||
	    rd32(base, size, off + 4, &nchildren))
		return -1;
	off += 8;
	if (skip_props(base, size, &off, nprops))
		return -1;
	for (uint32_t i = 0; i < nchildren; i++) {
		uint32_t child = off;
		if (node_end(base, size, child, &off, depth + 1))
			return -1;
	}
	*end = off;
	return 0;
}

static int node_name_is(uintptr_t base, uint64_t size, uint32_t node,
    const char *want)
{
	uint32_t nprops;
	if (rd32(base, size, node, &nprops))
		return 0;
	uint32_t off = node + 8;
	for (uint32_t i = 0; i < nprops; i++) {
		uint32_t len;
		if (!in_bounds(size, off, 36) ||
		    rd32(base, size, off + 32, &len))
			return 0;
		len &= 0x7fffffffU;
		if (prop_name_is(base, off, "name")) {
			if (!in_bounds(size, off + 36, len))
				return 0;
			const volatile unsigned char *p =
			    (const volatile unsigned char *)(base + off + 36);
			for (uint32_t j = 0; j < len; j++) {
				if (!want[j])
					return p[j] == 0;
				if (p[j] != (unsigned char)want[j])
					return 0;
			}
			return want[len] == 0;
		}
		uint64_t next = (uint64_t)off + 36 + ((len + 3U) & ~3U);
		if (next > size)
			return 0;
		off = (uint32_t)next;
	}
	return 0;
}

static int find_child(uintptr_t base, uint64_t size, uint32_t parent,
    const char *want, uint32_t *out)
{
	uint32_t nprops, nchildren;
	if (rd32(base, size, parent, &nprops) ||
	    rd32(base, size, parent + 4, &nchildren))
		return -1;
	uint32_t off = parent + 8;
	if (skip_props(base, size, &off, nprops))
		return -1;
	for (uint32_t i = 0; i < nchildren; i++) {
		uint32_t child = off;
		if (node_name_is(base, size, child, want)) {
			*out = child;
			return 0;
		}
		if (node_end(base, size, child, &off, 0))
			return -1;
	}
	return -1;
}

static int
set_u64(uintptr_t adt, uint64_t size, uint32_t node, const char *name, uint64_t value)
{
	uint32_t nprops;
	if (rd32(adt, size, node, &nprops))
		return -1;
	uint32_t off = node + 8;
	for (uint32_t i = 0; i < nprops; i++) {
		uint32_t len;
		if (!in_bounds(size, off, 36) ||
		    rd32(adt, size, off + 32, &len))
			return -1;
		if (prop_name_is(adt, off, name)) {
			if ((len & 0x7fffffffU) != 8 || (len & 0x80000000U))
				return -1;
			*(volatile uint64_t *)(adt + off + 36) = value;
			return 0;
		}
		len &= 0x7fffffffU;
		uint64_t next = (uint64_t)off + 36 + ((len + 3U) & ~3U);
		if (next > size)
			return -1;
		off = (uint32_t)next;
	}
	return -1;
}

int
adt_set_dram(uintptr_t adt, uint64_t size, uint64_t base, uint64_t mem_size)
{
	uint32_t chosen;
	if (!adt || size < 8 ||
	    find_child(adt, size, 0, "chosen", &chosen))
		return -1;
	if (set_u64(adt, size, chosen, "dram-base", base))
		return -1;
	return set_u64(adt, size, chosen, "dram-size", mem_size);
}

int
adt_set_pram(uintptr_t adt, uint64_t size, uint64_t base, uint64_t pram_size)
{
	uint32_t pram, nprops;
	if (!adt || size < 8 ||
	    find_child(adt, size, 0, "pram", &pram) ||
	    rd32(adt, size, pram, &nprops))
		return -1;
	uint32_t off = pram + 8;
	for (uint32_t i = 0; i < nprops; i++) {
		uint32_t len;
		if (!in_bounds(size, off, 36) ||
		    rd32(adt, size, off + 32, &len))
			return -1;
		if (prop_name_is(adt, off, "reg")) {
			if ((len & 0x7fffffffU) != 16 || (len & 0x80000000U))
				return -1;
			*(volatile uint64_t *)(adt + off + 36) = base;
			*(volatile uint64_t *)(adt + off + 44) = pram_size;
			return 0;
		}
		len &= 0x7fffffffU;
		uint64_t next = (uint64_t)off + 36 + ((len + 3U) & ~3U);
		if (next > size)
			return -1;
		off = (uint32_t)next;
	}
	return -1;
}

static int
node_u32_prop(uintptr_t adt, uint64_t size, uint32_t node, const char *name,
    uint32_t *value)
{
	uint32_t nprops;
	if (rd32(adt, size, node, &nprops))
		return -1;
	uint32_t off = node + 8;
	for (uint32_t i = 0; i < nprops; i++) {
		uint32_t len;
		if (!in_bounds(size, off, 36) ||
		    rd32(adt, size, off + 32, &len))
			return -1;
		if (prop_name_is(adt, off, name)) {
			if ((len & 0x7fffffffU) != 4 || (len & 0x80000000U))
				return -1;
			*value = *(const volatile uint32_t *)(adt + off + 36);
			return 0;
		}
		len &= 0x7fffffffU;
		uint64_t next = (uint64_t)off + 36 + ((len + 3U) & ~3U);
		if (next > size)
			return -1;
		off = (uint32_t)next;
	}
	return -1;
}

static int
set_node_u32_prop(uintptr_t adt, uint64_t size, uint32_t node,
    const char *name, uint32_t value)
{
	uint32_t nprops;
	if (rd32(adt, size, node, &nprops))
		return -1;
	uint32_t off = node + 8;
	for (uint32_t i = 0; i < nprops; i++) {
		uint32_t len;
		if (!in_bounds(size, off, 36) ||
		    rd32(adt, size, off + 32, &len))
			return -1;
		if (prop_name_is(adt, off, name)) {
			if ((len & 0x7fffffffU) != 4 || (len & 0x80000000U))
				return -1;
			*(volatile uint32_t *)(adt + off + 36) = value;
			return 0;
		}
		len &= 0x7fffffffU;
		uint64_t next = (uint64_t)off + 36 + ((len + 3U) & ~3U);
		if (next > size)
			return -1;
		off = (uint32_t)next;
	}
	return -1;
}

/*
 * The default ADT has one CPU child, so its "reg" has to become the live boot
 * MPIDR. An SMP ADT already carries the ten DTS MPIDRs: if the live MPIDR is
 * one of them, leave the tree alone; otherwise patch the first child, which
 * is what the one-CPU tree needs.
 */
int adt_set_boot_cpu_mpidr(uintptr_t adt, uint64_t size, uint32_t mpidr)
{
	uint32_t cpus, nprops, nchildren, first = 0;
	if (!adt || size < 8 ||
	    find_child(adt, size, 0, "cpus", &cpus))
		return -1;
	if (rd32(adt, size, cpus, &nprops) ||
	    rd32(adt, size, cpus + 4, &nchildren) ||
	    !nchildren)
		return -1;
	uint32_t off = cpus + 8;
	if (skip_props(adt, size, &off, nprops))
		return -1;
	for (uint32_t i = 0; i < nchildren; i++) {
		uint32_t child = off, reg;
		if (node_u32_prop(adt, size, child, "reg", &reg) == 0 &&
		    reg == mpidr)
			return 0;
		if (!first)
			first = child;
		if (node_end(adt, size, child, &off, 0))
			return -1;
	}
	if (!first)
		return -1;
	return set_node_u32_prop(adt, size, first, "reg", mpidr);
}
