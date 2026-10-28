/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdint.h>
#include "menu_config.h"
#include "menu_ui.h"
#include "fdt.h"

#ifndef FB_FALLBACK
#define FB_FALLBACK	0xFA200000UL
#endif
#ifndef FB_FALLBACK_SIZE
#define FB_FALLBACK_SIZE 0x009a4000UL
#endif
#define FB_W		1080U
#define FB_H		2340U
#define FB_STRIDE	(FB_W * 4U)

#ifndef KEY_BASE
#define KEY_BASE	0x12850000UL
#endif
#define KEY_DAT		0x64U

#ifndef LOG_BASE
#define LOG_BASE	0x91200000UL
#endif
#define LOG_MAGIC	"E1SMENU1"
#define LOG_HEADER	16U
#define LOG_MAX		0xff00U

#define STOCK_ALIGN	0x200000UL
#define POLL_MS		10U
#define LONG_MS		800U

extern void psci_reset(void) __attribute__((noreturn));

__attribute__((section(".menu_config"), used))
volatile struct menu_config menu_config = {
	.magic = { 'E', '1', 'S', 'M', 'E', 'N', 'U', '1' },
	.version = MENU_CONFIG_VERSION,
	.default_ms = 5000,
	.menu_ms = 10000,
};

_Static_assert(sizeof(struct menu_config) == 88, "menu config layout");

static struct ui_fb ui;
static uintptr_t fb_base = FB_FALLBACK;
static uint64_t fb_size = FB_FALLBACK_SIZE;
static uint32_t fb_w = FB_W, fb_h = FB_H, fb_stride = FB_STRIDE;
static int ui_ready;

static uint32_t log_pos;
static int log_ready;

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
	log_ready = 1;
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

static uint64_t timer_freq(void)
{
	uint64_t f;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
	if (!f) {
		f = 25600000;
		__asm__ volatile("msr cntfrq_el0, %0" : : "r"(f));
	}
	return f;
}

static uint64_t timer_now(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static void delay_ms(uint32_t ms)
{
	uint64_t ticks = (timer_freq() / 1000) * ms;
	uint64_t start = timer_now();

	while (timer_now() - start < ticks)
		;
}

#ifdef MENU_TEST_NO_KEYS
static int key_down(void)
{
	return 0;
}
#else
static int key_down(void)
{
	return (*(volatile uint32_t *)(KEY_BASE + KEY_DAT) & 1U) == 0;
}
#endif

#define GICD_BASE	0x10200000UL
#define GICR_BASE	0x10240000UL
#define GICD_PIDR2	0xfe8U
#define GICR_TYPER	0x8U

#ifdef MENU_TEST_NO_GIC
static int gic_probe(void)
{
	return 0;
}
#else
static int gic_probe(void)
{
	uint32_t pidr2 = *(volatile uint32_t *)(GICD_BASE + GICD_PIDR2);
	uint64_t typer = *(volatile uint64_t *)(GICR_BASE + GICR_TYPER);

	return ((pidr2 >> 4) & 0xf) == 3 && typer != 0 && typer != ~0ULL;
}
#endif

struct key_state {
	int stable;
	int count;
	int pressed;
	int reported;
	int ignore;
	uint32_t held;
};

static void key_state_init(struct key_state *ks)
{
	ks->stable = key_down();
	ks->count = 0;
	ks->pressed = 0;
	ks->reported = 0;
	ks->ignore = 0;
	ks->held = 0;
}

static void key_wait_release(struct key_state *ks, uint32_t ms)
{
	for (uint32_t t = 0; t < ms; t += POLL_MS) {
		if (!key_down()) {
			delay_ms(2 * POLL_MS);
			if (!key_down()) {
				key_state_init(ks);
				return;
			}
		}
		delay_ms(POLL_MS);
	}
	ks->pressed = 0;
	ks->reported = 0;
	ks->held = 0;
	ks->ignore = 1;
}

static int key_poll(struct key_state *ks, uint32_t ms)
{
	int down = key_down();

	if (ks->ignore) {
		if (!down)
			key_state_init(ks);
		else
			for (uint32_t t = 0; t < ms; t += POLL_MS)
				delay_ms(POLL_MS);
		return 0;
	}
	if (down != ks->stable) {
		ks->stable = down;
		ks->count = 0;
	}
	for (uint32_t t = 0; t < ms; t += POLL_MS) {
		delay_ms(POLL_MS);
		down = key_down();
		if (down == ks->stable) {
			if (ks->count < 2)
				ks->count++;
		} else {
			ks->stable = down;
			ks->count = 0;
		}
		if (ks->count >= 2) {
			if (ks->stable) {
				if (!ks->pressed) {
					ks->pressed = 1;
					ks->reported = 0;
					ks->held = 0;
				} else if (ks->held < 2000) {
					ks->held += POLL_MS;
				}
			} else if (ks->pressed) {
				int longp = ks->held >= LONG_MS;

				ks->pressed = 0;
				ks->held = 0;
				ks->reported = 0;
				return longp ? 2 : 1;
			}
		}
		if (ks->pressed && ks->held >= LONG_MS && !ks->reported) {
			ks->reported = 1;
			return 2;
		}
	}
	return 0;
}

static void clean_range(uintptr_t start, uintptr_t end)
{
	uint64_t ctr;
	uintptr_t line;

	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
	line = 4UL << ((ctr >> 16) & 0xf);
	for (uintptr_t p = start & ~(line - 1); p < end; p += line)
		__asm__ volatile("dc civac, %0" : : "r"(p) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static void clean_dtb(uintptr_t dtb)
{
	if (be32(dtb) == 0xd00dfeed) {
		uint32_t total = be32(dtb + 4);

		if (total < 0x1000 || total > 0x200000)
			total = 0x200000;
		clean_range(dtb, dtb + total);
	} else {
		clean_range(dtb, dtb + 0x200000);
	}
}

static int magic_ok(void)
{
	static const char want[8] = {
		'E', '1', 'S', 'M', 'E', 'N', 'U', '1'
	};

	for (int i = 0; i < 8; i++)
		if (menu_config.magic[i] != want[i])
			return 0;
	return 1;
}

static int config_valid(void)
{
	uint64_t stock_off = menu_config.stock_off;
	uint64_t stock_size = menu_config.stock_size;
	uint64_t total = menu_config.total_size;

	if (!magic_ok() || menu_config.version != MENU_CONFIG_VERSION)
		return 0;
	if (menu_config.flags & ~MENU_FLAG_KEY)
		return 0;
	if (stock_off < STOCK_ALIGN || (stock_off & (STOCK_ALIGN - 1)))
		return 0;
	if (stock_size < 64 || stock_off + stock_size > total)
		return 0;
	if (menu_config.stock_image_size < stock_size ||
	    menu_config.stock_image_size > (64UL << 20))
		return 0;
	if (menu_config.memory_size < stock_off + menu_config.stock_image_size)
		return 0;
	if (menu_config.xnu_size) {
		if (menu_config.xnu_off <
		    stock_off + menu_config.stock_image_size)
			return 0;
		if (menu_config.xnu_off & (STOCK_ALIGN - 1))
			return 0;
		if (menu_config.xnu_off + menu_config.xnu_size > total)
			return 0;
		if (menu_config.memory_size <
		    menu_config.xnu_off + menu_config.xnu_size)
			return 0;
	} else if (menu_config.xnu_off) {
		return 0;
	}
	return 1;
}

static int armd_ok(uintptr_t p)
{
	const volatile uint8_t *b = (const volatile uint8_t *)p;

	return b[0x38] == 'A' && b[0x39] == 'R' &&
	    b[0x3a] == 'M' && b[0x3b] == 'd';
}

static int stock_ok(uintptr_t p)
{
	uint64_t image, text;

	if (!armd_ok(p))
		return 0;
	text = *(volatile uint64_t *)(p + 8);
	image = *(volatile uint64_t *)(p + 16);
	if (text != 0 || image != menu_config.stock_image_size)
		return 0;
	return 1;
}

static int xnu_ok(uintptr_t p)
{
	uint64_t image;

	if (!armd_ok(p))
		return 0;
	image = *(volatile uint64_t *)(p + 16);
	if (image < 0x10000 || image > menu_config.xnu_size)
		return 0;
	return 1;
}

static void flush_fb(void)
{
	clean_range(fb_base, fb_base + fb_size);
}

static int fb_init(uintptr_t dtb)
{
	uint64_t size = FB_FALLBACK_SIZE;
	uint32_t w = FB_W, h = FB_H, stride = FB_STRIDE;
	uintptr_t base = FB_FALLBACK;
	int have;

	have = fdt_find_framebuffer(dtb, &base, &size, &w, &h, &stride);
	if (!have) {
		base = FB_FALLBACK;
		size = FB_FALLBACK_SIZE;
		w = FB_W;
		h = FB_H;
		stride = FB_STRIDE;
	}
	if (!stride || stride < w * 4U)
		stride = w * 4U;
	fb_base = base;
	fb_size = size;
	fb_w = w;
	fb_h = h;
	fb_stride = stride;
	ui.base = (volatile uint32_t *)base;
	ui.width = w;
	ui.height = h;
	ui.stride = stride / 4U;
	return have;
}

static uint64_t menu_fail(const char *why)
{
	log_str("\nFAIL: ");
	log_str(why);
	log_str("\n");
	if (ui_ready) {
		menu_ui_error(&ui, why);
		flush_fb();
	}
	return 0;
}

static uint64_t boot_android(uintptr_t stock, uintptr_t dtb)
{
	log_str("boot Android\n");
	clean_range(stock, stock + menu_config.stock_image_size);
	clean_dtb(dtb);
	return stock;
}

static uint64_t boot_xnu(uintptr_t xnu, uintptr_t dtb)
{
	log_str("boot XNU\n");
	clean_range(xnu, xnu + 0x20000);
	clean_dtb(dtb);
	return xnu;
}

uint64_t menu_main(uintptr_t load_base, uintptr_t dtb)
{
	uintptr_t stock, xnu;
	struct menu_info info;
	struct key_state ks;
	uint64_t el;
	int have_fb, def_sec, menu_sec, key_menu;

	log_init();
	log_str("E1S BOOT MENU\n");
	log_kv("load", load_base);
	log_kv("dtb", dtb);

	if (!config_valid()) {
		log_str("bad menu config\n");
		return 0;
	}
	stock = load_base + menu_config.stock_off;
	xnu = load_base + menu_config.xnu_off;
	log_kv("stock", stock);
	log_kv("stock size", menu_config.stock_size);
	log_kv("stock image", menu_config.stock_image_size);
	log_kv("xnu", menu_config.xnu_size ? xnu : 0);
	log_kv("xnu size", menu_config.xnu_size);
	log_kv("memory", menu_config.memory_size);
	log_str("\n");

	if (!stock_ok(stock))
		return menu_fail("stock kernel header is invalid");
	if (menu_config.xnu_size && !xnu_ok(xnu))
		return menu_fail("XNU image header is invalid");

	have_fb = fb_init(dtb);
	log_kv("fb", fb_base);
	log_kv("fb size", fb_size);
	log_kv("fb mode", ((uint64_t)fb_w << 32) | fb_h);
	log_kv("fb from dtb", (uint64_t)have_fb);
	log_str("\n");

	def_sec = (int)(menu_config.default_ms / 1000);
	menu_sec = (int)(menu_config.menu_ms / 1000);
	if (def_sec < 1)
		def_sec = 1;
	if (menu_sec < 1)
		menu_sec = 1;

	__asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
	info.kernel = "Linux 6.1.93 stock";
	info.cmdline = "stock boot.img cmdline is empty";
	info.load_base = load_base;
	info.stock_base = stock;
	info.stock_size = menu_config.stock_size;
	info.stock_image_size = menu_config.stock_image_size;
	info.memory_size = menu_config.memory_size;
	info.xnu_base = menu_config.xnu_size ? xnu : 0;
	info.xnu_size = menu_config.xnu_size;
	info.entry = stock;
	info.fb_base = fb_base;
	info.fb_size = fb_size;
	info.fb_w = fb_w;
	info.fb_h = fb_h;
	info.el = (uint32_t)(el >> 2);
	info.gic_ok = gic_probe();
	info.have_fb = have_fb;
	key_menu = (menu_config.flags & MENU_FLAG_KEY) != 0;
	info.menu = key_menu;
	log_kv("gic", (uint64_t)info.gic_ok);
	log_kv("default", (uint64_t)def_sec);
	log_kv("menu", (uint64_t)menu_sec);
	log_kv("keys", (uint64_t)key_menu);
	log_str("\n");

	menu_ui_begin(&ui, &info);
	flush_fb();
	ui_ready = 1;

	log_str("splash\n");
	if (key_menu)
		key_state_init(&ks);
	for (int s = def_sec; s > 0; s--) {
		menu_ui_splash(&ui, s, (def_sec - s) * 100 / def_sec, key_menu);
		flush_fb();
		if (!key_menu) {
			delay_ms(1000);
			continue;
		}
		int ev = key_poll(&ks, 1000);

		if (ev) {
			log_kv("menu key", (uint64_t)ev);
			key_wait_release(&ks, 3000);
			goto menu;
		}
	}
	menu_ui_splash(&ui, 0, 100, key_menu);
	flush_fb();
	delay_ms(200);
	log_str("default Android\n");
	return boot_android(stock, dtb);

menu:
	{
		int opts[4];
		const char *names[4] = {
			"Android", "HackiOS (XNU)", "Recovery", "Download"
		};
		const char *disp[4];
		int n = 0, sel = 0, sec = menu_sec;

		opts[n++] = 0;
		if (menu_config.xnu_size)
			opts[n++] = 1;
		opts[n++] = 2;
		opts[n++] = 3;
		for (int i = 0; i < n; i++)
			disp[i] = names[opts[i]];

		while (sec > 0) {
			menu_ui_options(&ui, disp, n, sel, sec,
			    (menu_sec - sec) * 100 / menu_sec);
			flush_fb();
			int ev = key_poll(&ks, 1000);

			if (ev == 1) {
				sel = (sel + 1) % n;
				sec = menu_sec;
				log_kv("menu select", (uint64_t)sel);
			} else if (ev == 2) {
				log_kv("menu pick", (uint64_t)sel);
				break;
			} else {
				sec--;
			}
		}
		if (sec <= 0) {
			sel = 0;
			log_str("menu timeout Android\n");
		}

		if (opts[sel] == 0)
			return boot_android(stock, dtb);
		if (opts[sel] == 1)
			return boot_xnu(xnu, dtb);
		if (opts[sel] == 2) {
			for (int s = 3; s > 0; s--) {
				menu_ui_message(&ui, "Recovery mode",
				    "Hold Volume Up + Power.",
				    "Keep holding through the reset.", s);
				flush_fb();
				delay_ms(1000);
			}
			psci_reset();
		}
		for (int s = 3; s > 0; s--) {
			menu_ui_message(&ui, "Download mode",
			    "Hold Volume Up + Volume Down.",
			    "Connect USB, release at the warning.", s);
			flush_fb();
			delay_ms(1000);
		}
		psci_reset();
	}
	return 0;
}

void menu_exception(uint64_t slot)
{
	uint64_t esr, elr, far, el;

	if (!log_ready)
		log_init();
	__asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
	if ((el & 0xc) == 0x8) {
		__asm__ volatile("mrs %0, esr_el2" : "=r"(esr));
		__asm__ volatile("mrs %0, elr_el2" : "=r"(elr));
		__asm__ volatile("mrs %0, far_el2" : "=r"(far));
	} else {
		__asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
		__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
		__asm__ volatile("mrs %0, far_el1" : "=r"(far));
	}
	log_str("\nEXCEPTION slot=");
	log_hex(slot);
	log_kv("el", el);
	log_kv("esr", esr);
	log_kv("elr", elr);
	log_kv("far", far);
	log_str("\n");
	for (;;)
		__asm__ volatile("wfe");
}
