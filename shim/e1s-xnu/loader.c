/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdint.h>
#include "ui.h"
#include "adt_patch.h"
#include "fdt.h"
#include "gic.h"

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

static uint32_t log_pos;
static uint64_t fb_base = FB_BASE;
static uint64_t fb_size = FB_SIZE;
static uint32_t fb_w = FB_W, fb_h = FB_H, fb_stride = FB_STRIDE;
static struct ui_fb ui;
static int ui_ready;

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

static void log_str(const char *s)
{
	while (*s)
		log_char(*s++);
}

static void log_hex(uint64_t v)
{
	static const char d[] = "0123456789abcdef";
	char b[19];
	int n = 0, started = 0;

	b[n++] = '0';
	b[n++] = 'x';
	for (int i = 60; i >= 0; i -= 4) {
		int x = (v >> i) & 0xf;
		if (x || started || i == 0) {
			b[n++] = d[x];
			started = 1;
		}
	}
	b[n] = 0;
	log_str(b);
}

static void log_kv(const char *k, uint64_t v)
{
	log_str(k);
	log_char('=');
	log_hex(v);
	log_char(' ');
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

static void fb_flush(void)
{
	clean((uintptr_t)fb_base, (uintptr_t)fb_base + fb_size);
}

static void ui_init(void)
{
	uint32_t stride = fb_stride;

	if (stride == 0 || stride < fb_w * 4U)
		stride = fb_w * 4U;
	ui.base = (volatile uint32_t *)(uintptr_t)fb_base;
	ui.width = fb_w;
	ui.height = fb_h;
	ui.stride = stride / 4U;
	ui_begin(&ui);
	ui_ready = 1;
}

static int cmdline_hold(const char *cmd)
{
	static const char key[] = "e1s.hold=";

	for (int i = 0; cmd[i]; i++) {
		int j = 0, v = 0, k;

		if (i && cmd[i - 1] != ' ')
			continue;
		while (key[j] && cmd[i + j] == key[j])
			j++;
		if (key[j])
			continue;
		k = i + j;
		while (cmd[k] >= '0' && cmd[k] <= '9') {
			v = v * 10 + (cmd[k] - '0');
			if (v > 30)
				v = 30;
			k++;
		}
		return v;
	}
	return 0;
}

static int starts_with(const char *s, const char *p)
{
	while (*p)
		if (*s++ != *p++)
			return 0;
	return 1;
}

static void cmdline_xnu(char *dst, const char *src)
{
	int n = 0;

	for (int i = 0; src[i] && n < BOOT_LINE_LENGTH - 1; ) {
		if ((i == 0 || src[i - 1] == ' ') &&
		    starts_with(&src[i], "e1s.hold=")) {
			int j = i + 9;

			while (src[j] >= '0' && src[j] <= '9')
				j++;
			while (src[j] == ' ')
				j++;
			i = j;
			continue;
		}
		dst[n++] = src[i++];
	}
	while (n > 0 && dst[n - 1] == ' ')
		n--;
	dst[n] = 0;
}

static void hold_seconds(int seconds)
{
	uint64_t freq, start, now, ticks;

	if (seconds <= 0)
		return;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
	if (!freq)
		return;
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
	ticks = freq * (uint64_t)seconds;
	do {
		__asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
	} while (now - start < ticks);
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
	log_str("\nFAIL: ");
	log_str(why);
	log_str("\n");
	if (ui_ready) {
		ui_fail(&ui, why);
		fb_flush();
	}
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
	log_str("S24XNU LOADER (experimental e1s)\n");
	log_str("Galaxy S24 SM-S921B e1s\n");
	log_str("Exynos 2400 S5E9945 10-core\n");

	int have_fb = fdt_find_framebuffer(dtb, &fb_base, &fb_size, &fb_w,
	    &fb_h, &fb_stride);
	ui_init();
	ui_set_stage(&ui, UI_STAGE_SBOOT, UI_OK);
	ui_set_stage(&ui, UI_STAGE_FB, have_fb ? UI_OK : UI_WARN);
	fb_flush();

	log_kv("EL", el);
	log_kv("SCTLR", sctlr);
	log_kv("DTB", dtb);
	log_kv("DTB FB", have_fb ? fb_base : 0);
	log_kv("FB", fb_base);
	log_kv("FB size", fb_size);
	log_kv("FB mode", ((uint64_t)fb_w << 32) | fb_h);
	log_str("\n");

	if (el != 1)
		return fail("not entered at EL1");

	struct gic_info gi = {0};
	int gic_rc = gic_prepare(&gi);
	log_kv("GIC", (uint64_t)gic_rc);
	log_kv("GICD", gi.gicd);
	log_kv("GICR", gi.gicr);
	log_kv("MPIDR", gi.mpidr);
	log_kv("SRE", (uint64_t)gi.sre);
	log_str("\n");
	ui_set_stage(&ui, UI_STAGE_GIC, gic_rc == 0 ? UI_OK : UI_WARN);

	ui_set_stage(&ui, UI_STAGE_PAYLOAD, UI_ACTIVE);
	const struct payload *p = (const struct payload *)payload_addr();
	if (!magic_ok(p->magic)) {
		uint64_t rd_start, rd_end;
		if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
		    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end) ||
		    rd_end <= rd_start) {
			ui_set_stage(&ui, UI_STAGE_PAYLOAD, UI_FAIL);
			return fail("no appended SWEETXNU payload or initrd");
		}
		log_kv("initrd", rd_start);
		log_str("\n");
		p = (const struct payload *)rd_start;
		if (!magic_ok(p->magic)) {
			ui_set_stage(&ui, UI_STAGE_PAYLOAD, UI_FAIL);
			return fail("initrd is not a SWEETXNU payload");
		}
	}
	log_kv("virt", p->virt_base);
	log_kv("entry", p->entry_va);
	log_kv("phys", p->phys_base);
	log_kv("mem", p->mem_size);
	log_kv("adt", p->adt_size);
	log_str("\n");

	if (p->version != 1 || !p->nsegs || p->nsegs > 32 ||
	    (p->adt_off & 7) || (p->adt_size & 7) ||
	    p->entry_va < p->virt_base || p->kernel_end < p->virt_base) {
		ui_set_stage(&ui, UI_STAGE_PAYLOAD, UI_FAIL);
		return fail("bad payload header");
	}
	if (!p->mem_size || p->mem_size > (4UL << 30)) {
		ui_set_stage(&ui, UI_STAGE_PAYLOAD, UI_FAIL);
		return fail("bad payload RAM size");
	}
	ui_set_stage(&ui, UI_STAGE_PAYLOAD, UI_OK);
	int hold = cmdline_hold(p->cmdline);
	char xnu_cmdline[BOOT_LINE_LENGTH];

	zero((uintptr_t)xnu_cmdline, sizeof(xnu_cmdline));
	cmdline_xnu(xnu_cmdline, p->cmdline);

	ui_set_stage(&ui, UI_STAGE_RAM, UI_ACTIVE);
	uint64_t win_base = p->phys_base;
	uint64_t win_size = p->mem_size;
	uintptr_t loader_image = (uintptr_t)_head;
	uint64_t loader_size = 0x40000 + (32UL << 20);
	if ((uintptr_t)p > loader_image)
		loader_size = ((uintptr_t)p - loader_image) + (32UL << 20);
	if (choose_window(dtb, loader_image, loader_size,
	    fb_base, fb_size, &win_base, &win_size)) {
		if (win_size > p->mem_size)
			win_size = p->mem_size;
		log_kv("window from DTB", win_base);
	} else {
		log_kv("window fallback", win_base);
	}
	log_kv("win", win_base);
	log_kv("win size", win_size);
	log_str("\n");
	if (win_base < 0x80000000ULL || win_size < MIN_WINDOW ||
	    win_base + win_size < win_base) {
		ui_set_stage(&ui, UI_STAGE_RAM, UI_FAIL);
		return fail("no usable RAM window");
	}

	uintptr_t mem_end = win_base + win_size;
	uintptr_t kernel_end = win_base + (p->kernel_end - p->virt_base);
	if (kernel_end >= mem_end) {
		ui_set_stage(&ui, UI_STAGE_RAM, UI_FAIL);
		return fail("kernel larger than RAM window");
	}

	uint64_t adt_end = p->adt_off + p->adt_size;
	if (adt_end < p->adt_off || adt_end > (256UL << 20)) {
		ui_set_stage(&ui, UI_STAGE_RAM, UI_FAIL);
		return fail("bad ADT offset/size");
	}
	uint64_t payload_len = adt_end;
	uint64_t seg_total = 0;
	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		if (s->vmaddr < p->virt_base || s->file_size > s->vmsize ||
		    s->vmsize > win_size ||
		    s->vmaddr - p->virt_base > win_size - s->vmsize) {
			ui_set_stage(&ui, UI_STAGE_RAM, UI_FAIL);
			return fail("bad segment");
		}
		uint64_t file_end = s->file_off + s->file_size;
		if (file_end < s->file_off || file_end > (256UL << 20)) {
			ui_set_stage(&ui, UI_STAGE_RAM, UI_FAIL);
			return fail("bad segment file range");
		}
		if (file_end > payload_len)
			payload_len = file_end;
		seg_total += s->file_size;
	}
	uintptr_t payload_start = (uintptr_t)p;
	uintptr_t payload_end = payload_start + payload_len;
	if (payload_start < mem_end && payload_end > win_base) {
		ui_set_stage(&ui, UI_STAGE_RAM, UI_FAIL);
		return fail("payload overlaps XNU RAM");
	}

	struct ui_boot_info info;
	info.have_fb = have_fb;
	info.fb_base = fb_base;
	info.fb_size = fb_size;
	info.fb_w = fb_w;
	info.fb_h = fb_h;
	info.fb_stride = fb_stride;
	info.el = el;
	info.gic_ok = gic_rc == 0;
	info.virt_base = p->virt_base;
	info.entry_va = p->entry_va;
	info.phys_base = win_base;
	info.mem_size = win_size;
	info.win_base = win_base;
	info.win_size = win_size;
	info.payload_size = payload_len;
	info.nsegs = p->nsegs;
	info.adt_size = p->adt_size;
	info.cmdline = xnu_cmdline;
	ui_set_info(&ui, &info);
	ui_set_stage(&ui, UI_STAGE_RAM, UI_OK);
	fb_flush();

	log_str("zeroing XNU RAM...\n");
	ui_set_stage(&ui, UI_STAGE_CLEAR, UI_ACTIVE);
	ui_set_progress(&ui, 0, "Clearing XNU RAM");
	ui_set_status(&ui, "0 MiB", UI_TEXT_DIM);
	fb_flush();
	{
		const uint64_t step = 16UL << 20;

		for (uint64_t off = 0; off < win_size; off += step) {
			char b[64];
			char *q = b;
			uint64_t n = win_size - off;

			if (n > step)
				n = step;
			zero(win_base + off, n);
			q = ui_put_dec(q, (off + n) >> 20);
			q = ui_put_str(q, " MiB");
			ui_set_progress(&ui,
			    (int)(((off + n) * 100) / win_size),
			    "Clearing XNU RAM");
			ui_set_status(&ui, b, UI_TEXT_DIM);
			fb_flush();
		}
	}
	ui_set_stage(&ui, UI_STAGE_CLEAR, UI_OK);
	log_str("zeroed\n");

	ui_set_stage(&ui, UI_STAGE_COPY, UI_ACTIVE);
	ui_set_progress(&ui, 0, "Copying kernel segments");
	ui_set_status(&ui, "0 segments", UI_TEXT_DIM);
	fb_flush();
	{
		uint64_t copied = 0;

		for (uint32_t i = 0; i < p->nsegs; i++) {
			const struct payload_seg *s = &p->segs[i];
			uintptr_t dst = win_base + (s->vmaddr - p->virt_base);
			char b[96];
			char *q = b;

			copy(dst, payload_start + s->file_off, s->file_size);
			zero(dst + s->file_size, s->vmsize - s->file_size);
			clean(dst, dst + s->vmsize);
			copied += s->file_size;
			q = ui_put_dec(q, i + 1);
			q = ui_put_str(q, " / ");
			q = ui_put_dec(q, p->nsegs);
			q = ui_put_str(q, " segments  ");
			q = ui_put_mb(q, copied);
			q = ui_put_str(q, " / ");
			q = ui_put_mb(q, seg_total);
			q = ui_put_str(q, " MB");
			ui_set_progress(&ui, seg_total ?
			    (int)((copied * 100) / seg_total) : 100,
			    "Copying kernel segments");
			ui_set_status(&ui, b, UI_TEXT_DIM);
			fb_flush();
			log_str("seg ");
			log_hex(i);
			log_str(" -> ");
			log_hex(dst);
			log_char(' ');
			log_hex(s->vmsize);
			log_char('\n');
		}
	}
	ui_set_stage(&ui, UI_STAGE_COPY, UI_OK);

	ui_set_stage(&ui, UI_STAGE_ADT, UI_ACTIVE);
	uintptr_t adt_pa = kernel_end;
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	if (ba_pa + PAGE_16K > mem_end) {
		ui_set_stage(&ui, UI_STAGE_ADT, UI_FAIL);
		return fail("no room for ADT/boot_args");
	}
	copy(adt_pa, (uintptr_t)p + p->adt_off, p->adt_size);
	if (adt_set_boot_cpu_mpidr(adt_pa, p->adt_size, (uint32_t)gi.mpidr)) {
		ui_set_stage(&ui, UI_STAGE_ADT, UI_FAIL);
		return fail("ADT boot CPU MPIDR patch failed");
	}
	uint64_t pram_base = (win_base + win_size - 0x100000ULL) & ~(PAGE_16K - 1);
	uint64_t pram_size = 0x80000;
	if (adt_set_dram(adt_pa, p->adt_size, win_base, win_size) ||
	    adt_set_pram(adt_pa, p->adt_size, pram_base, pram_size) ||
	    adt_rebase_ramdisk(adt_pa, p->adt_size,
	    (int64_t)(win_base - p->phys_base))) {
		ui_set_stage(&ui, UI_STAGE_ADT, UI_FAIL);
		return fail("ADT memory patch failed");
	}
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
		ba->CommandLine[i] = xnu_cmdline[i];
	ba->memSizeActual = win_size;
	clean(adt_pa, ba_pa + PAGE_16K);
	ui_set_stage(&ui, UI_STAGE_ADT, UI_OK);

	uintptr_t entry_pa = win_base + (p->entry_va - p->virt_base);
	if (entry_pa >= kernel_end) {
		ui_set_stage(&ui, UI_STAGE_ADT, UI_FAIL);
		return fail("entry outside kernel");
	}
	boot_args_pa = ba_pa;

	ui_set_stage(&ui, UI_STAGE_JUMP, UI_OK);
	ui_set_progress(&ui, 100, "Kernel ready");
	ui_set_status(&ui, "boot_args ready - jumping to XNU", UI_TEXT_OK);
	fb_flush();

	log_kv("adt_pa", adt_pa);
	log_kv("boot_args", ba_pa);
	log_kv("entry_pa", entry_pa);
	log_str("\ncmdline: ");
	for (int i = 0; i < BOOT_LINE_LENGTH && xnu_cmdline[i]; i++)
		log_char(xnu_cmdline[i]);
	log_kv("\nhold", (uint64_t)hold);
	log_str("\nJUMP TO XNU\n");
	clean(LOG_BASE, LOG_BASE + LOG_HEADER + log_pos);

	hold_seconds(hold);
	return entry_pa;
}

void loader_exception(uint64_t slot)
{
	uint64_t esr, elr, far;
	__asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile("mrs %0, far_el1" : "=r"(far));
	log_str("\nEXCEPTION slot=");
	log_hex(slot);
	log_kv("esr", esr);
	log_kv("elr", elr);
	log_kv("far", far);
	log_str("\n");
	if (ui_ready) {
		ui_set_stage(&ui, UI_STAGE_JUMP, UI_FAIL);
		ui_fail(&ui, "exception at EL1 - see log");
		fb_flush();
	}
	for (;;)
		__asm__ volatile("wfe");
}
