/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Draw the e1s XNU loader screen (shim/e1s-xnu/ui.c) on QEMU's ramfb.
 *
 * This is a screen simulation. No XNU is loaded, no Exynos device is touched
 * and nothing here proves a phone boot. It exists so the real loader layout
 * can be seen in a real QEMU framebuffer capture.
 */
#include <stdint.h>
#include "ui.h"

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

void probe_exception(uint64_t slot)
{
	uart_puts("\nE1S QEMU screen: exception slot ");
	uart_putc('0' + (int)(slot & 0xf));
	uart_puts("\n");
	for (;;)
		__asm__ volatile("wfe");
}

void probe_main(uintptr_t dtb)
{
	static const struct ui_boot_info info = {
		.have_fb = 1,
		.fb_base = FB_BASE,
		.fb_size = (uint64_t)FB_WIDTH * FB_HEIGHT * 4U,
		.fb_w = FB_WIDTH,
		.fb_h = FB_HEIGHT,
		.fb_stride = FB_STRIDE,
		.el = 1,
		.gic_ok = 1,
		.virt_base = 0xFFFFFFF004000000ULL,
		.entry_va = 0xFFFFFFF0074264A8ULL,
		.phys_base = 0xA0000000ULL,
		.mem_size = 0x20000000ULL,
		.win_base = 0xA0000000ULL,
		.win_size = 0x20000000ULL,
		.payload_size = 16262016ULL,
		.nsegs = 10,
		.adt_size = 16644,
		.cmdline = "debug=0x14e cpus=1 keepsyms=1 -v",
	};
	struct ui_fb fb = {
		.base = (volatile uint32_t *)FB_BASE,
		.width = FB_WIDTH,
		.height = FB_HEIGHT,
		.stride = FB_STRIDE / 4U,
	};

	(void)dtb;
	uart_init();
	ramfb_init();
	if (!ramfb_ready) {
		uart_puts("E1S QEMU screen: no ramfb device\n");
		for (;;)
			__asm__ volatile("wfe");
	}
	uart_puts("E1S QEMU screen: loader ui.c drawn, no XNU executed\n");

	ui_begin(&fb);
	for (int i = 0; i < UI_STAGE_COUNT; i++)
		ui_set_stage(&fb, i, UI_OK);
	ui_set_info(&fb, &info);
	ui_set_progress(&fb, 100, "Kernel ready");
	ui_set_status(&fb, "boot_args ready - jumping to XNU", UI_TEXT_OK);

	for (;;)
		__asm__ volatile("wfe");
}
