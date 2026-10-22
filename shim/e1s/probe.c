/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdint.h>
#include "font.h"

#ifdef PROBE_QEMU
#define FB_BASE          0x50000000UL
#define FB_WIDTH         640U
#define FB_HEIGHT        480U
#define LOG_BASE         0x50200000UL
#define GICD_BASE        0x08000000UL
#define GICR_BASE        0x080A0000UL
#define UART_BASE        0x09000000UL
#else
#define FB_BASE          0xFA200000UL
#define FB_WIDTH         1080U
#define FB_HEIGHT        2340U
#define LOG_BASE         0x91200000UL
#define GICD_BASE        0x10200000UL
#define GICR_BASE        0x10240000UL
#endif

#define FB_STRIDE        (FB_WIDTH * 4U)

#define LOG_MAGIC        "E1SPRB1"
#define LOG_HEADER_SIZE  16U
#define LOG_DATA_MAX     0xFF00U
#define UFS_HCI_BASE     0x17100000UL

#define CHAR_SCALE       2
#define CHAR_W           (8 * CHAR_SCALE)
#define CHAR_H           (16 * CHAR_SCALE)
#define COLS             (FB_WIDTH / CHAR_W)
#define ROWS             (FB_HEIGHT / CHAR_H)

extern char _head[];
extern char _end[];
void halt(void) __attribute__((noreturn));

static int screen_ready;
static int log_ready;
static int exception_busy;
static uint32_t log_pos;
static uint32_t screen_row;
static uint32_t screen_col;

static inline uint32_t rd32(uintptr_t a)
{
	return *(volatile uint32_t *)a;
}

static inline void wr32(uintptr_t a, uint32_t v)
{
	*(volatile uint32_t *)a = v;
}

#ifdef PROBE_QEMU
static void uart_init(void)
{
	wr32(UART_BASE + 0x30, 0);
	wr32(UART_BASE + 0x24, 1);
	wr32(UART_BASE + 0x28, 0);
	wr32(UART_BASE + 0x2c, 0x70);
	wr32(UART_BASE + 0x38, 0);
	wr32(UART_BASE + 0x44, 0x7ff);
	wr32(UART_BASE + 0x30, 0x301);
}

static void uart_putc(char c)
{
	while (rd32(UART_BASE + 0x18) & (1U << 5))
		;
	wr32(UART_BASE, (uint32_t)(unsigned char)c);
}

static void qemu_exit(int code)
{
	register uintptr_t x0 __asm__("x0") = 0x18;
	register uintptr_t x1 __asm__("x1") = (uintptr_t)code;

	__asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
	for (;;)
		__asm__ volatile("wfe");
}
#endif

#define READ_SYSREG(name) \
	static inline uint64_t read_##name(void) \
	{ \
		uint64_t v; \
		__asm__ volatile("mrs %0, " #name : "=r"(v)); \
		return v; \
	}

READ_SYSREG(CurrentEL)
READ_SYSREG(MPIDR_EL1)
READ_SYSREG(CNTFRQ_EL0)
READ_SYSREG(SCTLR_EL1)
READ_SYSREG(MIDR_EL1)
READ_SYSREG(ID_AA64MMFR0_EL1)
READ_SYSREG(ID_AA64PFR0_EL1)
READ_SYSREG(ESR_EL1)
READ_SYSREG(FAR_EL1)
READ_SYSREG(ELR_EL1)
READ_SYSREG(SPSR_EL1)
READ_SYSREG(ESR_EL2)
READ_SYSREG(FAR_EL2)
READ_SYSREG(ELR_EL2)
READ_SYSREG(SPSR_EL2)

static void log_init(void)
{
	volatile uint8_t *p = (volatile uint8_t *)LOG_BASE;
	const char *m = LOG_MAGIC;

	for (uint32_t i = 0; i < 8; i++)
		p[i] = (uint8_t)m[i];
	wr32(LOG_BASE + 8, 0);
	wr32(LOG_BASE + 12, 0);
	*(volatile uint8_t *)(LOG_BASE + LOG_HEADER_SIZE) = 0;
	log_pos = 0;
	log_ready = 1;
	__asm__ volatile("dsb sy" ::: "memory");
}

static void log_char(char c)
{
	volatile uint8_t *p;

	if (!log_ready || log_pos >= LOG_DATA_MAX)
		return;
	p = (volatile uint8_t *)(LOG_BASE + LOG_HEADER_SIZE + log_pos);
	p[0] = (uint8_t)c;
	p[1] = 0;
	log_pos++;
	wr32(LOG_BASE + 8, log_pos);
}

static void log_finish(void)
{
	if (!log_ready)
		return;
	wr32(LOG_BASE + 8, log_pos);
	__asm__ volatile("dsb sy" ::: "memory");
}

static void fill_rect(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
		      uint32_t color)
{
	volatile uint32_t *fb = (volatile uint32_t *)FB_BASE;
	uint32_t x1 = x0 + w;
	uint32_t y1 = y0 + h;

	if (x1 > FB_WIDTH)
		x1 = FB_WIDTH;
	if (y1 > FB_HEIGHT)
		y1 = FB_HEIGHT;
	for (uint32_t y = y0; y < y1; y++) {
		volatile uint32_t *row = fb + (y * (FB_STRIDE / 4));
		for (uint32_t x = x0; x < x1; x++)
			row[x] = color;
	}
	__asm__ volatile("dsb sy" ::: "memory");
}

static void screen_init(void)
{
	fill_rect(0, 0, FB_WIDTH, FB_HEIGHT, 0xFF000000U);
	screen_row = 0;
	screen_col = 0;
	screen_ready = 1;
}

static void screen_putc(char c)
{
	volatile uint32_t *fb = (volatile uint32_t *)FB_BASE;
	const unsigned char *glyph;
	uint32_t x;
	uint32_t y;

	if (!screen_ready)
		return;
	if (c == '\n') {
		screen_col = 0;
		screen_row++;
		if (screen_row >= ROWS)
			screen_row = 0;
		return;
	}
	if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e)
		c = '?';
	if (screen_col >= COLS) {
		screen_col = 0;
		screen_row++;
		if (screen_row >= ROWS)
			screen_row = 0;
	}
	glyph = font8x16[(unsigned char)c - 0x20];
	x = screen_col * CHAR_W;
	y = screen_row * CHAR_H;
	for (uint32_t gy = 0; gy < 16; gy++) {
		unsigned char bits = glyph[gy];
		for (uint32_t gx = 0; gx < 8; gx++) {
			if (!(bits & (0x80U >> gx)))
				continue;
			for (uint32_t sy = 0; sy < CHAR_SCALE; sy++) {
				volatile uint32_t *row =
					fb + ((y + gy * CHAR_SCALE + sy) *
					      (FB_STRIDE / 4));
				for (uint32_t sx = 0; sx < CHAR_SCALE; sx++)
					row[x + gx * CHAR_SCALE + sx] =
						0xFFFFFFFFU;
			}
		}
	}
	screen_col++;
}

static void out_char(char c)
{
#ifdef PROBE_QEMU
	uart_putc(c);
#endif
	if (screen_ready)
		screen_putc(c);
	if (log_ready)
		log_char(c);
}

static void out(const char *s)
{
	while (*s)
		out_char(*s++);
}

static void out_n(const char *s, uint32_t n)
{
	for (uint32_t i = 0; i < n && s[i]; i++) {
		char c = s[i];
		if (c == '\n' || c == '\r' || c == '\t')
			c = ' ';
		else if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e)
			c = '.';
		out_char(c);
	}
}

static void hex(uint64_t v)
{
	static const char digits[] = "0123456789abcdef";
	char b[19] = "0x";
	uint32_t n = 2;
	int started = 0;

	for (int i = 60; i >= 0; i -= 4) {
		unsigned d = (unsigned)((v >> i) & 0xFU);
		if (d || started || i == 0) {
			b[n++] = digits[d];
			started = 1;
		}
	}
	b[n] = 0;
	out(b);
}

static void kv(const char *k, uint64_t v)
{
	out(k);
	out_char('=');
	hex(v);
	out_char(' ');
}

static inline uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint64_t be64(const uint8_t *p)
{
	return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static uint32_t bounded_len(const uint8_t *p, uint32_t max)
{
	uint32_t n = 0;

	while (n < max && p[n])
		n++;
	return n;
}

static int prefix(const char *s, const char *p)
{
	while (*p) {
		if (*s++ != *p++)
			return 0;
	}
	return 1;
}

static void fdt_dump(uintptr_t dtb)
{
	const uint8_t *base = (const uint8_t *)dtb;
	const uint8_t *p;
	uint32_t total;
	uint32_t off_struct;
	uint32_t size_struct;
	uint32_t off_strings;
	uint32_t size_strings;
	uint32_t off_rsv;
	uint32_t version;
	uintptr_t s;
	uintptr_t end;
	int depth = 0;
	int mem_depth = -1;
	int chosen_depth = -1;

	if (!dtb) {
		out("DTB=0\n");
		return;
	}
	if (be32(base) != 0xD00DFEEDU) {
		out("DTB bad magic=");
		hex(be32(base));
		out("\n");
		return;
	}
	total = be32(base + 4);
	off_struct = be32(base + 8);
	off_strings = be32(base + 12);
	off_rsv = be32(base + 16);
	version = be32(base + 20);
	size_strings = be32(base + 32);
	size_struct = be32(base + 36);
	if (total > 0x400000U || off_struct >= total || off_strings >= total ||
	    size_struct > total || size_strings > total) {
		out("DTB bad header\n");
		return;
	}
	kv("fdt", dtb);
	kv("total", total);
	kv("ver", version);
	out("\n");
	if (off_rsv + 16 <= total) {
		p = base + off_rsv;
		for (unsigned i = 0; i < 4; i++) {
			uint64_t a = be64(p);
			uint64_t z = be64(p + 8);
			if (!a && !z)
				break;
			out("rsv ");
			hex(a);
			out_char('+');
			hex(z);
			out("\n");
			p += 16;
		}
	}
	s = dtb + off_struct;
	end = s + size_struct;
	while (s + 4 <= end) {
		uint32_t tok = be32((const uint8_t *)s);
		s += 4;
		if (tok == 1) {
			const uint8_t *name = (const uint8_t *)s;
			uint32_t n = bounded_len(name, (uint32_t)(end - s));
			s += (n + 4U) & ~3U;
			depth++;
			if (depth == 1) {
				out("root=");
				out_n((const char *)name, n);
				out("\n");
			}
			if (prefix((const char *)name, "memory"))
				mem_depth = depth;
			if (prefix((const char *)name, "chosen"))
				chosen_depth = depth;
			continue;
		}
		if (tok == 2) {
			if (depth == mem_depth)
				mem_depth = -1;
			if (depth == chosen_depth)
				chosen_depth = -1;
			depth--;
			continue;
		}
		if (tok == 3) {
			uint32_t len;
			uint32_t nameoff;
			const uint8_t *val;
			const char *pname;

			if (s + 8 > end)
				break;
			len = be32((const uint8_t *)s);
			nameoff = be32((const uint8_t *)(s + 4));
			if (len > end - s - 8)
				break;
			val = (const uint8_t *)(s + 8);
			if (nameoff < size_strings)
				pname = (const char *)(base + off_strings + nameoff);
			else
				pname = "?";
			if (depth == 1 && (prefix(pname, "model") ||
					   prefix(pname, "compatible"))) {
				out(pname);
				out_char('=');
				out_n((const char *)val, len);
				out("\n");
			}
			if (depth == mem_depth && prefix(pname, "reg")) {
				out("  mem");
				for (uint32_t i = 0; i + 4 <= len && i < 32; i += 4) {
					out_char(' ');
					hex(be32(val + i));
				}
				out("\n");
			}
			if (depth == chosen_depth) {
				if (prefix(pname, "bootargs")) {
					out("  bootargs=");
					out_n((const char *)val, len > 240 ? 240 : len);
					out("\n");
				} else if (prefix(pname, "linux,initrd-start")) {
					out("  initrd-start=");
					if (len >= 8)
						hex(be64(val));
					else if (len >= 4)
						hex(be32(val));
					out("\n");
				} else if (prefix(pname, "linux,initrd-end")) {
					out("  initrd-end=");
					if (len >= 8)
						hex(be64(val));
					else if (len >= 4)
						hex(be32(val));
					out("\n");
				}
			}
			s += 8 + ((len + 3U) & ~3U);
			continue;
		}
		if (tok == 4)
			continue;
		if (tok == 9)
			break;
		break;
	}
}

static void gic_dump(void)
{
	uint64_t typer = rd32(GICD_BASE + 4);
	uint64_t iidr = rd32(GICD_BASE + 8);
	uint64_t r0 = rd32(GICR_BASE + 8);
	uint64_t r1 = rd32(GICR_BASE + 12);

	kv("GICD_TYPER", typer);
	kv("GICD_IIDR", iidr);
	out("\nGICR0_TYPER ");
	hex(r0);
	out_char(' ');
	hex(r1);
	out("\n");
}

#ifndef PROBE_QEMU
static void ufs_dump(void)
{
	kv("UFS_CAP", rd32(UFS_HCI_BASE + 0x00));
	kv("UFS_VER", rd32(UFS_HCI_BASE + 0x08));
	kv("UFS_HCS", rd32(UFS_HCI_BASE + 0x30));
	out("\n");
}
#endif

void probe_exception(uint64_t slot)
{
	uint64_t el;

	if (exception_busy)
		halt();
	exception_busy = 1;
#ifdef PROBE_QEMU
	uart_init();
#endif
	screen_ready = 0;
	if (!log_ready)
		log_init();
	out("\nEXCEPTION slot=");
	hex(slot);
	out("\n");
	el = read_CurrentEL() & 0xCU;
	kv("CurrentEL", el);
	out("\n");
	if (el == 8U) {
		kv("ESR_EL2", read_ESR_EL2());
		kv("FAR_EL2", read_FAR_EL2());
		out("\n");
		kv("ELR_EL2", read_ELR_EL2());
		kv("SPSR_EL2", read_SPSR_EL2());
		out("\n");
	} else {
		kv("ESR_EL1", read_ESR_EL1());
		kv("FAR_EL1", read_FAR_EL1());
		out("\n");
		kv("ELR_EL1", read_ELR_EL1());
		kv("SPSR_EL1", read_SPSR_EL1());
		out("\n");
	}
	log_finish();
	fill_rect(0, 0, FB_WIDTH, 160, 0x00FF0000U);
#ifdef PROBE_QEMU
	qemu_exit(1);
#endif
	halt();
}

void probe_main(uintptr_t dtb)
{
#ifdef PROBE_QEMU
	uart_init();
#endif
	log_init();
	screen_init();
	out("E1S PROBE v1\n");
#ifdef PROBE_QEMU
	out("QEMU virt smoke test (not S24 hardware)\n");
#else
	out("SM-S921B e1s S5E9945\n");
#endif
	out("image ");
	hex((uintptr_t)_head);
	out_char('-');
	hex((uintptr_t)_end);
	out("\n");
	kv("EL", read_CurrentEL());
	kv("MPIDR", read_MPIDR_EL1());
	out("\n");
	kv("CNTFRQ", read_CNTFRQ_EL0());
	kv("SCTLR", read_SCTLR_EL1());
	out("\n");
	kv("MIDR", read_MIDR_EL1());
	kv("MMFR0", read_ID_AA64MMFR0_EL1());
	out("\n");
	kv("PFR0", read_ID_AA64PFR0_EL1());
	kv("DTB", dtb);
	out("\n\nFDT\n");
	fdt_dump(dtb);
	out("\nGIC\n");
	gic_dump();
	out("\nUFS\n");
#ifdef PROBE_QEMU
	out("QEMU virt has no e1s UFS; skipped\n");
#else
	ufs_dump();
#endif
	out("\nPROBE DONE\n");
	log_finish();
#ifdef PROBE_QEMU
	out("QEMU EXIT\n");
	qemu_exit(0);
#endif
	out("halt\n");
	halt();
}
