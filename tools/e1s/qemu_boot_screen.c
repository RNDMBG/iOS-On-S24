/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Draw the e1s boot flow on QEMU's ramfb from a built boot image.
 *
 * Frame 1 is the real loader UI (shim/e1s-xnu/ui.c) with the values parsed
 * out of out/e1s-boot-apfs.img. Frame 2 is the expected XNU console.
 *
 * No XNU is executed and no Exynos hardware is emulated. Both frames are
 * simulations derived from the image bytes.
 */
#include <stdint.h>
#include "ui.h"
#include "bootinfo.h"
#include "../../shim/e1s/font.h"

#define FB_BASE          0x50000000UL
#define FB_WIDTH         1080U
#define FB_HEIGHT        2340U
#define FB_STRIDE        (FB_WIDTH * 4U)
#define UART_BASE        0x09000000UL
#define FW_CFG_BASE      0x09020000UL
#define RAMFB_XRGB8888   0x34325258U

#define FW_CFG_FILE_DIR  0x0019U
#define FW_CFG_DMA_ERROR 0x0001U
#define FW_CFG_DMA_READ  0x0002U
#define FW_CFG_DMA_SELECT 0x0008U
#define FW_CFG_DMA_WRITE 0x0010U

struct fw_cfg_dma {
	uint32_t control;
	uint32_t length;
	uint64_t address;
} __attribute__((packed));

struct ramfb_config {
	uint64_t address;
	uint32_t fourcc;
	uint32_t flags;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
} __attribute__((packed));

static struct fw_cfg_dma fw_dma __attribute__((aligned(8)));
static uint8_t fw_dir[0x1000] __attribute__((aligned(8)));
static struct ramfb_config ramfb_cfg __attribute__((aligned(8)));
static int ramfb_ready;

static inline uint16_t be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void uart_putc(char c)
{
	while (*(volatile uint32_t *)(UART_BASE + 0x18) & (1U << 5))
		;
	*(volatile uint32_t *)UART_BASE = (uint32_t)(unsigned char)c;
}

static void uart_puts(const char *s)
{
	while (*s)
		uart_putc(*s++);
}

static void uart_puthex(uint64_t v)
{
	static const char d[] = "0123456789abcdef";

	uart_puts("0x");
	for (int i = 15; i >= 0; i--) {
		uart_putc(d[(v >> (i * 4)) & 0xf]);
		if (i == 12)
			uart_putc('_');
	}
	uart_putc('\n');
}

static void uart_init(void)
{
	*(volatile uint32_t *)(UART_BASE + 0x30) = 0;
	*(volatile uint32_t *)(UART_BASE + 0x24) = 1;
	*(volatile uint32_t *)(UART_BASE + 0x28) = 0;
	*(volatile uint32_t *)(UART_BASE + 0x2c) = 0x70;
	*(volatile uint32_t *)(UART_BASE + 0x38) = 0;
	*(volatile uint32_t *)(UART_BASE + 0x44) = 0x7ff;
	*(volatile uint32_t *)(UART_BASE + 0x30) = 0x301;
}

static int fw_cfg_dma(uint32_t control, const void *data, uint32_t len)
{
	volatile struct fw_cfg_dma *dma = &fw_dma;

	dma->control = __builtin_bswap32(control);
	dma->length = __builtin_bswap32(len);
	dma->address = __builtin_bswap64((uint64_t)(uintptr_t)data);
	__asm__ volatile("dsb sy" ::: "memory");
	*(volatile uint64_t *)(FW_CFG_BASE + 16) =
	    __builtin_bswap64((uint64_t)(uintptr_t)&fw_dma);
	for (uint32_t i = 0; i < 1000000U; i++) {
		uint32_t c = __builtin_bswap32(dma->control);

		if (!c)
			return 0;
		if (c & FW_CFG_DMA_ERROR)
			return -1;
	}
	return -1;
}

static int name_eq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

static void ramfb_init(void)
{
	uint32_t count;
	uint16_t sel = 0;

	if (fw_cfg_dma((FW_CFG_FILE_DIR << 16) | FW_CFG_DMA_SELECT |
	    FW_CFG_DMA_READ, fw_dir, sizeof(fw_dir)))
		return;
	count = be32(fw_dir);
	if (count > 0x3fU)
		count = 0x3fU;
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *entry = fw_dir + 4 + i * 64;
		const char *name = (const char *)(entry + 8);

		if (name_eq(name, "etc/ramfb")) {
			sel = be16(entry + 4);
			break;
		}
	}
	if (!sel)
		return;

	ramfb_cfg.address = __builtin_bswap64(FB_BASE);
	ramfb_cfg.fourcc = __builtin_bswap32(RAMFB_XRGB8888);
	ramfb_cfg.flags = 0;
	ramfb_cfg.width = __builtin_bswap32(FB_WIDTH);
	ramfb_cfg.height = __builtin_bswap32(FB_HEIGHT);
	ramfb_cfg.stride = __builtin_bswap32(FB_STRIDE);
	if (fw_cfg_dma((sel << 16) | FW_CFG_DMA_SELECT | FW_CFG_DMA_WRITE,
	    &ramfb_cfg, sizeof(ramfb_cfg)))
		return;
	ramfb_ready = 1;
}

static void spin_ms(uint32_t ms)
{
	uint64_t freq, start, now, ticks;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
	ticks = freq / 1000U * ms;
	do {
		__asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
	} while (now - start < ticks);
}

static void rect(const struct ui_fb *fb, int x, int y, int w, int h,
    uint32_t color)
{
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (w <= 0 || h <= 0 || (uint32_t)x >= fb->width ||
	    (uint32_t)y >= fb->height)
		return;
	if (x + w > (int)fb->width)
		w = (int)fb->width - x;
	if (y + h > (int)fb->height)
		h = (int)fb->height - y;
	for (int yy = 0; yy < h; yy++) {
		volatile uint32_t *row = fb->base +
		    (uint64_t)(y + yy) * fb->stride;
		for (int xx = 0; xx < w; xx++)
			row[x + xx] = color;
	}
}

static void text(const struct ui_fb *fb, int x, int y, const char *s,
    uint32_t color, int scale)
{
	for (int i = 0; s[i]; i++) {
		unsigned char ch = (unsigned char)s[i];
		const unsigned char *g;

		if (ch < 0x20 || ch > 0x7e)
			ch = '?';
		g = font8x16[ch - 0x20];
		for (int gy = 0; gy < 16; gy++)
			for (int gx = 0; gx < 8; gx++)
				if (g[gy] & (0x80 >> gx))
					rect(fb, x + i * 8 * scale + gx * scale,
					    y + gy * scale, scale, scale, color);
	}
}

static const char *const console_lines[] = {
	"e1s-log: console ring at physical 0x91300000, 1024 KiB; dump 2 MiB at 0x91200000",
	"e1s-display: framebuffer 0xfa200000 1080x2340 stride 4320 depth 32",
	"e1s-acpm: mailbox 0x12b90000, channel 2, firmware table at 0x0205ed00",
	"e1s-pmic: S2MPS27 0x0d/0x1d, S2MPS28-4 0x0b/0x5f: read-only",
	"e1s-battery: MAX77775 at 0x36: status, soc, cell, current, temp",
	"e1s-ufs: HCI 0x17100000, MCQ queue 0, read-only",
	"apfs: volume 0 \"ROOT\": 2 files, 5 directories, 5 blocks of data",
	"BSD root: md0, major 3, minor 0",
	"load_init_program: attempting to load /sbin/launchd",
	"*** SWEET userspace: init running as PID 1 ***",
	"SWEET init: /dev/md1 mounted",
	"SWEET init: still alive",
};

static const struct ui_boot_info boot_info = {
	.have_fb = 1,
	.fb_base = 0xFA200000ULL,
	.fb_size = 0x9A4000ULL,
	.fb_w = FB_WIDTH,
	.fb_h = FB_HEIGHT,
	.fb_stride = FB_STRIDE,
	.el = 1,
	.gic_ok = 1,
	.virt_base = BI_VIRT_BASE,
	.entry_va = BI_ENTRY,
	.phys_base = BI_PHYS_BASE,
	.mem_size = BI_MEM_SIZE,
	.win_base = BI_PHYS_BASE,
	.win_size = BI_MEM_SIZE,
	.payload_size = BI_PAYLOAD_BYTES,
	.nsegs = BI_NSEGS,
	.adt_size = BI_ADT_BYTES,
	.cmdline = BI_CMDLINE,
};

static struct ui_fb screen = {
	.base = (volatile uint32_t *)FB_BASE,
	.width = FB_WIDTH,
	.height = FB_HEIGHT,
	.stride = FB_STRIDE / 4U,
};

#ifndef STEP_MS
#define STEP_MS 250
#endif

#ifndef HOLD_MS
#define HOLD_MS 1500
#endif

static const char *const step_labels[UI_STAGE_COUNT] = {
	"S-Boot handoff",
	"Framebuffer",
	"GICv3",
	"Payload header",
	"RAM window",
	"Clear XNU RAM",
	"Copy kernel segments",
	"ADT + boot_args",
	"Jump to XNU",
};

static void draw_loader_begin(void)
{
	ui_begin(&screen);
	ui_set_info(&screen, &boot_info);
	ui_set_progress(&screen, 0, "Waiting for boot data");
	ui_set_status(&screen, "", UI_TEXT_MUTED);
}

static void loader_step(int i)
{
	char b[64];
	char *p;
	int percent = (i + 1) * 100 / UI_STAGE_COUNT;

	ui_set_stage(&screen, i, UI_ACTIVE);
	p = ui_put_dec(b, (uint64_t)percent);
	p = ui_put_str(p, "%  ");
	p = ui_put_str(p, step_labels[i]);
	ui_set_progress(&screen, percent, b);
	spin_ms(STEP_MS);
	ui_set_stage(&screen, i, UI_OK);
	spin_ms(STEP_MS / 4);
}

static void draw_loader_done(void)
{
	ui_set_progress(&screen, 100, "Kernel ready");
	ui_set_status(&screen, "boot_args ready - jumping to XNU", UI_TEXT_OK);
}

static void draw_console(void)
{
	struct ui_fb fb = {
		.base = (volatile uint32_t *)FB_BASE,
		.width = FB_WIDTH,
		.height = FB_HEIGHT,
		.stride = FB_STRIDE / 4U,
	};
	int y = 60;

	rect(&fb, 0, 0, FB_WIDTH, FB_HEIGHT, 0xff000000U);
	text(&fb, 40, 24, "SIMULATED BOOT CONSOLE - NO XNU EXECUTED", 0xfffbbf24U, 2);
	text(&fb, 40, 88, "Derived from " BI_IMAGE_NAME
	    ": real header values, simulated output.", 0xff9ca3afU, 1);
	y = 140;
	for (unsigned i = 0; i < sizeof(console_lines) / sizeof(console_lines[0]);
	    i++) {
		text(&fb, 40, y, console_lines[i], 0xffe5e7ebU, 1);
		y += 26;
	}
	y += 24;
	text(&fb, 40, y, "boot-args:", 0xff9ca3afU, 1);
	text(&fb, 40, y + 20, BI_CMDLINE, 0xffe5e7ebU, 1);
	y += 72;
	text(&fb, 40, y, "This is the expected console, not a capture. On the phone the same",
	    0xff9ca3afU, 1);
	text(&fb, 40, y + 20, "lines are recoverable from 0x91200000. No image has run on hardware.",
	    0xff9ca3afU, 1);
}

void probe_exception(uint64_t slot)
{
	uart_puts("\nE1S QEMU boot screen: exception slot ");
	uart_putc('0' + (int)(slot & 0xf));
	uart_puts("\n");
	for (;;)
		__asm__ volatile("wfe");
}

void probe_main(uintptr_t dtb)
{
	(void)dtb;
	uart_init();
	uart_puts("load address: ");
	uart_puthex((uint64_t)(uintptr_t)&probe_main);
	ramfb_init();
	if (!ramfb_ready) {
		uart_puts("E1S QEMU boot screen: no ramfb device\n");
		for (;;)
			__asm__ volatile("wfe");
	}
	uart_puts("E1S QEMU boot screen: frame 1 (loader UI), frame 2 (console)\n");

	draw_loader_begin();
	for (int i = 0; i < UI_STAGE_COUNT; i++)
		loader_step(i);
	draw_loader_done();
	spin_ms(HOLD_MS);
	draw_console();

	for (;;)
		__asm__ volatile("wfe");
}
