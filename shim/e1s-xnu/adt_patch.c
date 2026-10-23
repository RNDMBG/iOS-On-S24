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

int adt_set_boot_cpu_mpidr(uintptr_t adt, uint64_t size, uint32_t mpidr)
{
	uint32_t cpus, cpu, nprops, nchildren;
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
	cpu = off;
	if (rd32(adt, size, cpu, &nprops))
		return -1;
	off = cpu + 8;
	for (uint32_t i = 0; i < nprops; i++) {
		uint32_t len;
		if (!in_bounds(size, off, 36) ||
		    rd32(adt, size, off + 32, &len))
			return -1;
		if (prop_name_is(adt, off, "reg")) {
			if ((len & 0x7fffffffU) != 4 || (len & 0x80000000U))
				return -1;
			*(volatile uint32_t *)(adt + off + 36) = mpidr;
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
