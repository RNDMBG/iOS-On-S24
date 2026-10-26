/* SPDX-License-Identifier: GPL-2.0-only
 * Experimental Galaxy S24 (SM-S921B, e1s) XNU loader.
 *
 * This is not a working iOS boot: it only knows how to copy a SWEETXNU
 * payload out of the appended image, build boot_args and jump to XNU with
 * the MMU off. The e1s RAM window and timer values are still assumptions.
 */
#include <stdint.h>
#include "../e1s/font.h"
#include "adt_patch.h"
#include "fdt.h"
#include "gic.h"

/* What Samsung's bootloader normally leaves behind; the S-Boot device tree can
 * name a different base and the loader prefers that. */
#define FB_BASE		0xFA200000UL
#define FB_SIZE		0x009a4000UL
#define FB_W		1080U
#define FB_H		2340U
#define FB_STRIDE	(FB_W * 4U)
#define LOG_BASE	0x91200000UL
#define LOG_RESERVE	0x200000UL	/* sec_debug_next: loader log + XNU ring */
#define LOG_MAGIC	"E1SXNU01"
#define LOG_HEADER	16U
#define LOG_MAX		0xff00U

#define BOOT_LINE_LENGTH 608
#define PAGE_16K	0x4000UL
#define XNU_RAM_BASE	0x880000000UL
#define XNU_RAM_END	0x900000000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

uint64_t boot_args_pa;
extern char _head[];
extern uintptr_t payload_addr(void);
void halt(void) __attribute__((noreturn));

static uint32_t log_pos, row, col;

static inline void wr32(uintptr_t a, uint32_t v)
{
	*(volatile uint32_t *)a = v;
}

static void log_init(void)
{
	volatile char *p = (volatile char *)LOG_BASE;
	for (int i = 0; i < 8; i++)
		p[i] = LOG_MAGIC[i];
	wr32(LOG_BASE + 8, 0);
	wr32(LOG_BASE + 12, 0);
	*(volatile char *)(LOG_BASE + LOG_HEADER) = 0;
	log_pos = 0;
}

static void log_char(char c)
{
	if (log_pos >= LOG_MAX)
		return;
	*(volatile char *)(LOG_BASE + LOG_HEADER + log_pos++) = c;
	wr32(LOG_BASE + 8, log_pos);
}

static uint64_t fb_base = FB_BASE;
static uint64_t fb_size = FB_SIZE;
static uint32_t fb_w = FB_W, fb_h = FB_H, fb_stride = FB_STRIDE;

static void fill(int x0, int y0, int w, int h, uint32_t c)
{
	volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)fb_base;
	if (x0 < 0) { w += x0; x0 = 0; }
	if (y0 < 0) { h += y0; y0 = 0; }
	if (x0 + w > (int)fb_w) w = (int)fb_w - x0;
	if (y0 + h > (int)fb_h) h = (int)fb_h - y0;
	for (int y = y0; y < y0 + h; y++)
		for (int x = x0; x < x0 + w; x++)
			fb[y * (fb_stride / 4) + x] = c;
}

static void screen_putc(char c)
{
	if (c == '\n' || col >= fb_w / 16U) {
		col = 0;
		row++;
		if (row >= fb_h / 32U)
			row = 0;
		if (c == '\n')
			return;
	}
	if (c < 0x20 || c > 0x7e)
		c = '?';
	const unsigned char *g = font8x16[c - 0x20];
	int x0 = (int)col * 16, y0 = (int)row * 32;
	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 8; gx++)
			fill(x0 + gx * 2, y0 + gy * 2, 2, 2,
			     (g[gy] & (0x80 >> gx)) ? 0xffffffffU : 0xff000000U);
	col++;
}

static void out(const char *s)
{
	while (*s) {
		log_char(*s);
		screen_putc(*s++);
	}
}

static void hex(uint64_t v)
{
	static const char d[] = "0123456789abcdef";
	char b[19] = "0x";
	int n = 2, started = 0;
	for (int i = 60; i >= 0; i -= 4) {
		int x = (v >> i) & 0xf;
		if (x || started || i == 0) {
			b[n++] = d[x];
			started = 1;
		}
	}
	b[n] = 0;
	out(b);
}

static void kv(const char *k, uint64_t v)
{
	out(k);
	out("=");
	hex(v);
	out(" ");
}

static void clean(uintptr_t start, uintptr_t end)
{
	uint64_t ctr;
	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
	uintptr_t line = 4UL << ((ctr >> 16) & 0xf);
	for (uintptr_t p = start & ~(line - 1); p < end; p += line)
		__asm__ volatile("dc civac, %0" : : "r"(p) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

static void copy(uintptr_t dst, uintptr_t src, uint64_t n)
{
	while (n && ((dst | src) & 7)) {
		*(volatile uint8_t *)dst++ = *(volatile const uint8_t *)src++;
		n--;
	}
	while (n >= 8) {
		*(volatile uint64_t *)dst = *(volatile const uint64_t *)src;
		dst += 8;
		src += 8;
		n -= 8;
	}
	while (n--)
		*(volatile uint8_t *)dst++ = *(volatile const uint8_t *)src++;
}

static void zero(uintptr_t dst, uint64_t n)
{
	while (n && (dst & 7)) {
		*(volatile uint8_t *)dst++ = 0;
		n--;
	}
	while (n >= 8) {
		*(volatile uint64_t *)dst = 0;
		dst += 8;
		n -= 8;
	}
	while (n--)
		*(volatile uint8_t *)dst++ = 0;
}

static int magic_ok(const char *m)
{
	const char *want = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

static uint64_t fail(const char *why)
{
	out("\nFAIL: ");
	out(why);
	out("\n");
	return 0;
}

#define MAX_RANGES	96
#define MIN_WINDOW	(256UL << 20)
#define MAX_WINDOW	(512UL << 20)

static void
ranges_sort(struct fdt_range *r, int n)
{
	for (int i = 1; i < n; i++) {
		struct fdt_range v = r[i];
		int j = i - 1;
		while (j >= 0 && r[j].base > v.base) {
			r[j + 1] = r[j];
			j--;
		}
		r[j + 1] = v;
	}
}

static void
consider_gap(uint64_t start, uint64_t end, uint64_t *best_base, uint64_t *best_size)
{
	uint64_t base = (start + 0x1fffffUL) & ~0x1fffffUL;
	uint64_t size;

	if (end <= base)
		return;
	size = end - base;
	if (size > MAX_WINDOW)
		size = MAX_WINDOW;
	size &= ~0x1fffffUL;
	if (size < MIN_WINDOW || size <= *best_size)
		return;
	*best_base = end - size;
	*best_size = size;
}

/*
 * Pick XNU's physical window from the memory map S-Boot passes in the DTB,
 * avoiding reserved-memory nodes, the framebuffer, the log, the boot loader
 * image and the DTB itself.  Returns 0 when the DTB has no usable /memory
 * node; the caller then keeps the payload's built-in window.
 */
static int
choose_window(uintptr_t dtb, uintptr_t loader_image, uint64_t loader_size,
    uint64_t fb_base, uint64_t fb_size, uint64_t *out_base, uint64_t *out_size)
{
	struct fdt_range mem[16], occ[MAX_RANGES];
	int nmem = fdt_memory_ranges(dtb, mem, 16);
	int nocc;
	uint64_t best_base = 0, best_size = 0;

	if (nmem <= 0)
		return 0;
	nocc = fdt_reserved_ranges(dtb, occ, MAX_RANGES - 4);
	if (nocc < 0)
		nocc = 0;
	occ[nocc].base = fb_base;
	occ[nocc++].size = fb_size;
	occ[nocc].base = LOG_BASE;
	occ[nocc++].size = LOG_RESERVE;
	occ[nocc].base = loader_image;
	occ[nocc++].size = loader_size;
	occ[nocc].base = dtb;
	occ[nocc++].size = 0x200000;
	ranges_sort(occ, nocc);

	for (int i = 0; i < nmem; i++) {
		uint64_t cursor = mem[i].base;
		uint64_t end = mem[i].base + mem[i].size;

		if (end <= cursor)
			continue;
		for (int j = 0; j < nocc; j++) {
			uint64_t rbase = occ[j].base, rsize = occ[j].size;
			if (rsize == 0 || rbase >= end)
				continue;
			if (rbase + rsize <= cursor)
				continue;
			if (rbase > cursor) {
				consider_gap(cursor, rbase, &best_base, &best_size);
			}
			if (rbase + rsize > cursor)
				cursor = rbase + rsize;
			if (cursor >= end)
				break;
		}
		if (cursor < end) {
			consider_gap(cursor, end, &best_base, &best_size);
		}
	}
	if (best_size < MIN_WINDOW)
		return 0;
	*out_base = best_base;
	*out_size = best_size;
	return 1;
}

uint64_t loader_main(uintptr_t dtb, uint64_t el, uint64_t sctlr)
{
	log_init();
	int have_fb = fdt_find_framebuffer(dtb, &fb_base, &fb_size, &fb_w, &fb_h, &fb_stride);
	fill(0, 0, fb_w, fb_h, 0xff000000U);
	out("S24XNU LOADER (experimental e1s)\n");
	kv("EL", el);
	kv("SCTLR", sctlr);
	kv("DTB", dtb);
	kv("DTB FB", have_fb ? fb_base : 0);
	kv("FB", fb_base);
	kv("FB size", fb_size);
	kv("FB mode", ((uint64_t)fb_w << 32) | fb_h);
	out("\n");
	if (el != 1)
		return fail("not entered at EL1");

	struct gic_info gi;
	int gic_rc = gic_prepare(&gi);
	kv("GIC", (uint64_t)gic_rc);
	kv("GICD", gi.gicd);
	kv("GICR", gi.gicr);
	kv("MPIDR", gi.mpidr);
	kv("SRE", (uint64_t)gi.sre);
	out("\n");

	/* Appended payload first. If it is absent, accept the S8/S-Boot initrd
	 * convention so this loader can be tested without the append step. */
	const struct payload *p = (const struct payload *)payload_addr();
	if (!magic_ok(p->magic)) {
		uint64_t rd_start, rd_end;
		if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
		    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end) ||
		    rd_end <= rd_start) {
			return fail("no appended SWEETXNU payload or initrd");
		}
		kv("initrd", rd_start);
		out("\n");
		p = (const struct payload *)rd_start;
		if (!magic_ok(p->magic))
			return fail("initrd is not a SWEETXNU payload");
	}
	kv("virt", p->virt_base);
	kv("entry", p->entry_va);
	kv("phys", p->phys_base);
	kv("mem", p->mem_size);
	kv("adt", p->adt_size);
	out("\n");

	if (p->version != 1 || !p->nsegs || p->nsegs > 32 ||
	    (p->adt_off & 7) || (p->adt_size & 7) ||
	    p->entry_va < p->virt_base || p->kernel_end < p->virt_base)
		return fail("bad payload header");
	if (!p->mem_size || p->mem_size > (4UL << 30))
		return fail("bad payload RAM size");

	/*
	 * The payload's phys_base is only the fallback.  Pick the real window
	 * from S-Boot's /memory and /reserved-memory nodes: the old built-in
	 * value (0x880000000) is inside the high DRAM bank Samsung reserves for
	 * the modem in the e1s device tree.
	 */
	uint64_t win_base = p->phys_base;
	uint64_t win_size = p->mem_size;
	/*
	 * Keep the loader and the appended payload out of the window: S-Boot
	 * loads the whole Image (loader + payload) contiguously, and XNU is
	 * about to zero the window.  _head is the first byte of the Image.
	 */
	uintptr_t loader_image = (uintptr_t)_head;
	uint64_t loader_size = 0x40000 + (32UL << 20);
	if ((uintptr_t)p > loader_image)
		loader_size = ((uintptr_t)p - loader_image) + (32UL << 20);
	if (choose_window(dtb, loader_image, loader_size,
	    fb_base, fb_size, &win_base, &win_size)) {
		if (win_size > p->mem_size)
			win_size = p->mem_size;
		kv("window from DTB", win_base);
	} else {
		kv("window fallback", win_base);
	}
	kv("win", win_base);
	kv("win size", win_size);
	out("\n");
	if (win_base < 0x80000000ULL || win_size < MIN_WINDOW ||
	    win_base + win_size < win_base)
		return fail("no usable RAM window");

	uintptr_t mem_end = win_base + win_size;
	uintptr_t kernel_end = win_base + (p->kernel_end - p->virt_base);
	if (kernel_end >= mem_end)
		return fail("kernel larger than RAM window");

	/* Validate the whole payload before zeroing anything. An appended
	 * payload can land at any address S-Boot chooses; if that overlaps the
	 * XNU window, zeroing first would destroy the source. */
	uint64_t adt_end = p->adt_off + p->adt_size;
	if (adt_end < p->adt_off || adt_end > (256UL << 20))
		return fail("bad ADT offset/size");
	uint64_t payload_len = adt_end;
	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		if (s->vmaddr < p->virt_base || s->file_size > s->vmsize ||
		    s->vmsize > win_size ||
		    s->vmaddr - p->virt_base > win_size - s->vmsize)
			return fail("bad segment");
		uint64_t file_end = s->file_off + s->file_size;
		if (file_end < s->file_off || file_end > (256UL << 20))
			return fail("bad segment file range");
		if (file_end > payload_len)
			payload_len = file_end;
	}
	uintptr_t payload_start = (uintptr_t)p;
	uintptr_t payload_end = payload_start + payload_len;
	if (payload_start < mem_end && payload_end > win_base)
		return fail("payload overlaps XNU RAM");

	out("zeroing XNU RAM...\n");
	zero(win_base, win_size);
	out("zeroed\n");

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = win_base + (s->vmaddr - p->virt_base);
		copy(dst, payload_start + s->file_off, s->file_size);
		zero(dst + s->file_size, s->vmsize - s->file_size);
		clean(dst, dst + s->vmsize);
		out("seg ");
		hex(i);
		out(" -> ");
		hex(dst);
		out(" ");
		hex(s->vmsize);
		out("\n");
	}

	uintptr_t adt_pa = kernel_end;
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	if (ba_pa + PAGE_16K > mem_end)
		return fail("no room for ADT/boot_args");
	copy(adt_pa, (uintptr_t)p + p->adt_off, p->adt_size);
	if (adt_set_boot_cpu_mpidr(adt_pa, p->adt_size, (uint32_t)gi.mpidr))
		return fail("ADT boot CPU MPIDR patch failed");
	/* The ADT carries the generator's assumed map; replace it with the one
	 * this phone actually has.  /pram goes near the top of the window. */
	uint64_t pram_base = (win_base + win_size - 0x100000ULL) & ~(PAGE_16K - 1);
	uint64_t pram_size = 0x80000;
	if (adt_set_dram(adt_pa, p->adt_size, win_base, win_size) ||
	    adt_set_pram(adt_pa, p->adt_size, pram_base, pram_size))
		return fail("ADT memory patch failed");
	zero(ba_pa, PAGE_16K);

	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = win_base;
	ba->memSize = win_size;
	ba->topOfKernelData = ba_pa + PAGE_16K;
	ba->Video.v_baseAddr = fb_base;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = fb_stride;
	ba->Video.v_width = fb_w;
	ba->Video.v_height = fb_h;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = p->virt_base + (adt_pa - win_base);
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = win_size;
	clean(adt_pa, ba_pa + PAGE_16K);

	uintptr_t entry_pa = win_base + (p->entry_va - p->virt_base);
	if (entry_pa >= kernel_end)
		return fail("entry outside kernel");
	boot_args_pa = ba_pa;

	kv("adt_pa", adt_pa);
	kv("boot_args", ba_pa);
	kv("entry_pa", entry_pa);
	out("\ncmdline: ");
	for (int i = 0; i < BOOT_LINE_LENGTH && p->cmdline[i]; i++) {
		char c[2] = { p->cmdline[i], 0 };
		out(c);
	}
	out("\nJUMP TO XNU\n");
	clean(LOG_BASE, LOG_BASE + LOG_HEADER + log_pos);
	return entry_pa;
}

void loader_exception(uint64_t slot)
{
	uint64_t esr, elr, far;
	__asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile("mrs %0, far_el1" : "=r"(far));
	out("\nEXCEPTION slot=");
	hex(slot);
	kv("esr", esr);
	kv("elr", elr);
	kv("far", far);
	out("\n");
	for (;;)
		__asm__ volatile("wfe");
}
