/* SPDX-License-Identifier: GPL-2.0-only */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static int str_starts(const char *s, const char *prefix)
{
	while (*prefix)
		if (*s++ != *prefix++)
			return 0;
	return 1;
}

static int str_has(const char *s, const char *want)
{
	for (; *s; s++) {
		const char *a = s, *b = want;
		while (*a && *b && *a == *b)
			a++, b++;
		if (!*b)
			return 1;
	}
	return 0;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);
	uintptr_t strings = fdt + be32(fdt + 12);
	int depth = 0, in_chosen = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				if (len == 4)
					*out = be32(val);
				else if (len == 8)
					*out = (uint64_t)be32(val) << 32 | be32(val + 4);
				else
					return 0;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:
			return 0;
		}
	}
}

static void
add_range(struct fdt_range *out, int max, int *count, uint64_t base, uint64_t size)
{
	if (size == 0 || *count >= max)
		return;
	out[*count].base = base;
	out[*count].size = size;
	(*count)++;
}

/*
 * Decode one "reg" property with the node's #address-cells/#size-cells.
 * Samsung's trees use 2/1 (a 12-byte entry), the usual FDT default is 2/2,
 * and simple test trees use 1/1, so all three shapes must work.
 */
static void
decode_reg(uintptr_t val, uint32_t len, uint32_t ac, uint32_t sc,
    struct fdt_range *out, int max, int *count)
{
	if (ac > 0 && sc > 0 && ac <= 2 && sc <= 2) {
		uint32_t stride = 4 * (ac + sc);
		for (uint32_t off = 0; off + stride <= len; off += stride) {
			uint64_t base = 0, size = 0;
			for (uint32_t i = 0; i < ac; i++)
				base = base << 32 | be32(val + off + 4 * i);
			for (uint32_t i = 0; i < sc; i++)
				size = size << 32 | be32(val + off + 4 * (ac + i));
			add_range(out, max, count, base, size);
		}
		return;
	}
	if (len == 8) {
		add_range(out, max, count, be32(val), be32(val + 4));
	} else if (len >= 16) {
		add_range(out, max, count,
		    (uint64_t)be32(val) << 32 | be32(val + 4),
		    (uint64_t)be32(val + 8) << 32 | be32(val + 12));
	}
}

static uintptr_t
reg_walk(uintptr_t p, uintptr_t strings, int depth, uint32_t parent_ac,
    uint32_t parent_sc, int in_target, int mode, struct fdt_range *out,
    int max, int *count)
{
	uint32_t tok = be32(p);
	p += 4;
	if (tok != FDT_BEGIN_NODE || depth > 12)
		return p;

	const char *name = (const char *)p;
	p = align4(skip_str(p));

	int target = in_target;
	if (depth == 1) {
		if (mode == 0 && str_starts(name, "memory"))
			target = 1;
		if (mode == 1 && streq(name, "reserved-memory"))
			target = 1;
	}

	uint32_t ac = parent_ac, sc = parent_sc;
	uintptr_t regval = 0;
	uint32_t reglen = 0;

	for (;;) {
		tok = be32(p);
		p += 4;
		if (tok == FDT_PROP) {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			const char *prop = (const char *)(strings + nameoff);

			if (streq(prop, "#address-cells") && len == 4)
				ac = be32(val);
			else if (streq(prop, "#size-cells") && len == 4)
				sc = be32(val);
			else if (target && streq(prop, "reg")) {
				regval = val;
				reglen = len;
			}
			p = align4(val + len);
		} else if (tok == FDT_BEGIN_NODE) {
			p = reg_walk(p - 4, strings, depth + 1, ac, sc, target,
			    mode, out, max, count);
		} else if (tok == FDT_END_NODE) {
			break;
		} else if (tok == FDT_NOP) {
			continue;
		} else {
			break;
		}
	}

	if (target && regval)
		decode_reg(regval, reglen, ac, sc, out, max, count);
	return p;
}

int
fdt_memory_ranges(uintptr_t fdt, struct fdt_range *out, int max)
{
	int count = 0;

	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	reg_walk(fdt + be32(fdt + 8), fdt + be32(fdt + 12), 0, 2, 1, 0, 0,
	    out, max, &count);
	return count;
}

int
fdt_reserved_ranges(uintptr_t fdt, struct fdt_range *out, int max)
{
	int count = 0;

	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	reg_walk(fdt + be32(fdt + 8), fdt + be32(fdt + 12), 0, 2, 1, 0, 1,
	    out, max, &count);
	return count;
}

/*
 * Find the display that S-Boot left behind.  The bootloader's device tree has
 * a reserved-memory (or simple-framebuffer) node for it; its reg is in DRAM,
 * which is what tells it apart from the DECON/DSIM register blocks.  Returns
 * 1 and fills the description when one is found; 0 leaves the caller's
 * built-in guess in place.
 */
struct fb_candidate {
	uint64_t base, size;
	uint32_t width, height, stride;
	int found, score;
};

static void
fb_scan_node(const char *name, uint64_t base, uint64_t size, int have_reg,
    int simple, uint32_t width, uint32_t height, uint32_t stride,
    struct fb_candidate *best)
{
	int score = 0;

	/* Only RAM: the panel's register blocks are below 0x80000000. */
	if (!have_reg || base < 0x80000000ULL || base >= 0x100000000ULL ||
	    size < 4ULL * 1024 * 1024 || size > 64ULL * 1024 * 1024)
		return;
	if (simple || str_has(name, "framebuffer"))
		score = 4;
	else if (str_has(name, "splash") || str_has(name, "fb"))
		score = 3;
	else if (str_has(name, "display"))
		score = 2;
	if (score == 0)
		return;
	/*
	 * More than one node can look like a display.  A framebuffer/splash
	 * node beats a generic "display" reservation; among equals the smaller
	 * region is more likely to be the panel buffer than a large carveout.
	 */
	if (best->found &&
	    (score < best->score || (score == best->score && size > best->size)))
		return;
	best->found = 1;
	best->score = score;
	best->base = base;
	best->size = size;
	best->width = width;
	best->height = height;
	best->stride = stride;
}

static uintptr_t
fb_walk(uintptr_t p, uintptr_t strings, int depth, uint32_t parent_ac,
    uint32_t parent_sc, struct fb_candidate *best)
{
	uint32_t tok = be32(p);
	p += 4;
	if (tok != FDT_BEGIN_NODE || depth > 12)
		return p;

	const char *name = (const char *)p;
	p = align4(skip_str(p));

	uint32_t ac = parent_ac, sc = parent_sc;
	uint64_t base = 0, size = 0;
	uint32_t width = 0, height = 0, stride = 0;
	int have_reg = 0, simple = 0;
	uintptr_t regval = 0;
	uint32_t reglen = 0;

	for (;;) {
		tok = be32(p);
		p += 4;
		if (tok == FDT_PROP) {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			const char *prop = (const char *)(strings + nameoff);

			if (streq(prop, "#address-cells") && len == 4) {
				ac = be32(val);
			} else if (streq(prop, "#size-cells") && len == 4) {
				sc = be32(val);
			} else if (streq(prop, "reg")) {
				regval = val;
				reglen = len;
			} else if (streq(prop, "width") && len == 4) {
				width = be32(val);
			} else if (streq(prop, "height") && len == 4) {
				height = be32(val);
			} else if ((streq(prop, "stride") || streq(prop, "line-size") ||
			    streq(prop, "pitch")) && len == 4) {
				stride = be32(val);
			} else if (streq(prop, "compatible")) {
				for (uint32_t off = 0; off < len; off++) {
					if (str_has((const char *)val + off, "simple-framebuffer")) {
						simple = 1;
						break;
					}
					while (off < len && *(const char *)(val + off))
						off++;
				}
			}
			p = align4(val + len);
		} else if (tok == FDT_BEGIN_NODE) {
			p = fb_walk(p - 4, strings, depth + 1, ac, sc, best);
		} else if (tok == FDT_END_NODE) {
			break;
		} else if (tok == FDT_NOP) {
			continue;
		} else {
			break;
		}
	}

	if (regval && reglen) {
		struct fdt_range r = { 0 };
		int count = 0;
		decode_reg(regval, reglen, ac, sc, &r, 1, &count);
		if (count) {
			base = r.base;
			size = r.size;
			have_reg = 1;
		}
	}
	fb_scan_node(name, base, size, have_reg, simple, width, height, stride, best);
	return p;
}

int fdt_find_framebuffer(uintptr_t fdt, uint64_t *base, uint64_t *size,
    uint32_t *width, uint32_t *height, uint32_t *stride)
{
	struct fb_candidate best = { 0 };

	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);
	uintptr_t strings = fdt + be32(fdt + 12);
	fb_walk(p, strings, 0, 2, 1, &best);
	if (!best.found)
		return 0;
	*base = best.base;
	*size = best.size;
	if (best.width >= 320 && best.height >= 320) {
		*width = best.width;
		*height = best.height;
	}
	if (best.width && best.stride >= best.width * 4)
		*stride = best.stride;
	return 1;
}
