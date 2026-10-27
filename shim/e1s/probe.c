/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdint.h>
#include "font.h"

#ifdef PROBE_QEMU
#define FB_BASE          0x50000000UL
#define FB_WIDTH         1080U
#define FB_HEIGHT        2340U
#define LOG_BASE         0x50A00000UL
#define GICD_BASE        0x08000000UL
#define GICR_BASE        0x080A0000UL
#define UART_BASE        0x09000000UL
#define FW_CFG_BASE      0x09020000UL
#define PCI_ECAM_BASE    0x4010000000UL
#define PCI_MMIO_BASE    0x10000000UL
#define UFS_UTRL_BASE    0x50B00000UL
#define UFS_CMD_BASE     0x50C00000UL
#define UFS_DATA_BASE    0x50D00000UL
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

#define SCREEN_MARGIN_X  16U
#define SCREEN_MARGIN_Y  16U
#define CHAR_SCALE       2
#define CHAR_W           (8 * CHAR_SCALE)
#define CHAR_H           (16 * CHAR_SCALE)
#define COLS             ((FB_WIDTH - 2 * SCREEN_MARGIN_X) / CHAR_W)
#define ROWS             ((FB_HEIGHT - 2 * SCREEN_MARGIN_Y) / CHAR_H)

extern char _head[];
extern char _end[];
void halt(void) __attribute__((noreturn));
static void out(const char *s);
static void hex(uint64_t v);
static inline uint32_t be32(const uint8_t *p);
static inline uint16_t be16(const uint8_t *p);

#ifdef PROBE_QEMU
static int ramfb_ready;
#endif

static int screen_ready;
static int log_ready;
static int exception_busy;
static uint32_t log_pos;
static uint32_t screen_row;
static uint32_t screen_col;
static int screen_dirty_row;

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
	volatile uint64_t block[2] = { 0x20026U, (uint64_t)(unsigned)code };
	register uintptr_t x0 __asm__("x0") = 0x18;
	register uintptr_t x1 __asm__("x1") = (uintptr_t)block;

	__asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
	for (;;)
		__asm__ volatile("wfe");
}

#define FW_CFG_FILE_DIR     0x0019U
#define FW_CFG_DMA_ERROR    0x0001U
#define FW_CFG_DMA_READ     0x0002U
#define FW_CFG_DMA_SELECT   0x0008U
#define FW_CFG_DMA_WRITE    0x0010U
#define RAMFB_XRGB8888      0x34325258U

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

static int ramfb_name_eq(const char *a, const char *b)
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

		if (ramfb_name_eq(name, "etc/ramfb")) {
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

static uintptr_t pci_cfg(uint32_t dev, uint32_t fn, uint32_t reg)
{
	return PCI_ECAM_BASE + (dev << 15) + (fn << 12) + reg;
}

static uintptr_t pci_ufs_probe(void)
{
	for (uint32_t dev = 0; dev < 32; dev++) {
		uint32_t id = rd32(pci_cfg(dev, 0, 0));
		uintptr_t cfg;
		uint32_t bar;
		uint32_t mask;
		uint32_t size;
		uintptr_t base;
		uint32_t cmd;

		if (id != 0x00131B36U)
			continue;
		cfg = pci_cfg(dev, 0, 0);
		bar = rd32(cfg + 0x10);
		if (bar & 1U)
			continue;
		if (bar & 4U)
			continue;
		wr32(cfg + 0x10, 0xFFFFFFFFU);
		mask = rd32(cfg + 0x10) & ~0xFU;
		size = ~mask + 1U;
		if (size < 0x1000U)
			size = 0x1000U;
		base = (PCI_MMIO_BASE + size - 1U) & ~((uintptr_t)size - 1U);
		wr32(cfg + 0x10, (uint32_t)base);
		cmd = rd32(cfg + 0x04);
		wr32(cfg + 0x04, cmd | 0x6U);
		return base;
	}
	return 0;
}

static void zero_bytes(uintptr_t addr, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++)
		*(volatile uint8_t *)(addr + i) = 0;
}

static int wait_bits(uintptr_t base, uint32_t reg, uint32_t mask,
		     uint32_t want, uint32_t tries)
{
	for (uint32_t i = 0; i < tries; i++) {
		if ((rd32(base + reg) & mask) == want)
			return 0;
	}
	return -1;
}

static int ufs_qemu_setup(uintptr_t base)
{
	const uintptr_t utrl = UFS_UTRL_BASE;

	wr32(base + 0x34, 1);
	if (wait_bits(base, 0x34, 1, 1, 10000000)) {
		out("UFS_READ_FAIL hce\n");
		return -1;
	}
	wr32(base + 0x24, 0x470);
	if (wait_bits(base, 0x30, 0x8, 0x8, 10000000)) {
		out("UFS_READ_FAIL ready\n");
		return -1;
	}

	wr32(base + 0x94, 0);
	wr32(base + 0x98, 0);
	wr32(base + 0x9c, 0);
	wr32(base + 0x90, 0x16);
	if (wait_bits(base, 0x20, 0x400, 0x400, 10000000)) {
		out("UFS_READ_FAIL uic\n");
		return -1;
	}
	wr32(base + 0x20, 0x400);
	if (rd32(base + 0x98)) {
		out("UFS_READ_FAIL uicarg\n");
		return -1;
	}
	if (wait_bits(base, 0x30, 0xb, 0xb, 10000000)) {
		out("UFS_READ_FAIL link\n");
		return -1;
	}

	wr32(base + 0x24, 0x471);
	wr32(base + 0x4c, 0);
	wr32(base + 0x50, (uint32_t)utrl);
	wr32(base + 0x54, 0);
	wr32(base + 0x60, 1);
	return 0;
}

static int ufs_qemu_cmd(uintptr_t base, const char *tag, const uint8_t *cdb,
			uint32_t len, uint32_t flags)
{
	const uintptr_t cmd = UFS_CMD_BASE;
	const uintptr_t utrl = UFS_UTRL_BASE;
	const uintptr_t data = UFS_DATA_BASE;
	uint8_t *rsp = (uint8_t *)(cmd + 1024);
	uint32_t ocs;

	zero_bytes(utrl, 32);
	zero_bytes(cmd, 4096);
	zero_bytes(data, len ? len : 1);

	((volatile uint8_t *)cmd)[0] = 0x01;
	((volatile uint8_t *)cmd)[1] = (uint8_t)flags;
	((volatile uint8_t *)cmd)[12] = (uint8_t)(len >> 24);
	((volatile uint8_t *)cmd)[13] = (uint8_t)(len >> 16);
	((volatile uint8_t *)cmd)[14] = (uint8_t)(len >> 8);
	((volatile uint8_t *)cmd)[15] = (uint8_t)len;
	for (uint32_t i = 0; i < 16; i++)
		((volatile uint8_t *)cmd)[16 + i] = cdb[i];

	wr32(cmd + 2048, (uint32_t)data);
	wr32(cmd + 2052, 0);
	wr32(cmd + 2056, 0);
	wr32(cmd + 2060, len ? len - 1 : 0);

	wr32(utrl + 0, 0x15000000);
	wr32(utrl + 4, 0);
	wr32(utrl + 8, 0x0f);
	wr32(utrl + 12, 0);
	wr32(utrl + 16, (uint32_t)cmd);
	wr32(utrl + 20, 0);
	wr32(utrl + 24, 288 | (256U << 16));
	wr32(utrl + 28, (len ? 1U : 0U) | (512U << 16));
	__asm__ volatile("dsb sy" ::: "memory");

	wr32(base + 0x58, 1);
	if (wait_bits(base, 0x20, 1, 1, 10000000)) {
		out(tag);
		out("_FAIL doorbell\n");
		return -1;
	}
	wr32(base + 0x20, 1);

	ocs = rd32(utrl + 8) & 0xff;
	out(tag);
	out(" ocs=");
	hex(ocs);
	out(" rsp=");
	hex(rsp[6]);
	out(" status=");
	hex(rsp[7]);
	out(" data=");
	hex(be32((const uint8_t *)data));
	out("\n");
	return 0;
}

static int ufs_qemu_read(uintptr_t base)
{
	static const uint8_t sense_cdb[16] = { 0x03, 0, 0, 0, 18 };
	static const uint8_t read_cdb[16] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0 };

	if (ufs_qemu_setup(base))
		return -1;
	ufs_qemu_cmd(base, "UFS_SENSE", sense_cdb, 18, 0x40);
	return ufs_qemu_cmd(base, "UFS_READ", read_cdb, 4096, 0x40);
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
	screen_dirty_row = 0;
	screen_ready = 1;
}

static void screen_clear_row(uint32_t row)
{
	fill_rect(0, SCREEN_MARGIN_Y + row * CHAR_H, FB_WIDTH, CHAR_H,
	    0xFF000000U);
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
		screen_dirty_row = 1;
		return;
	}
	if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e)
		c = '?';
	if (screen_col >= COLS) {
		screen_col = 0;
		screen_row++;
		if (screen_row >= ROWS)
			screen_row = 0;
		screen_dirty_row = 1;
	}
	if (screen_dirty_row) {
		screen_clear_row(screen_row);
		screen_dirty_row = 0;
	}
	glyph = font8x16[(unsigned char)c - 0x20];
	x = SCREEN_MARGIN_X + screen_col * CHAR_W;
	y = SCREEN_MARGIN_Y + screen_row * CHAR_H;
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

static inline uint16_t be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
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

static void ufs_dump(void)
{
	uintptr_t base = UFS_HCI_BASE;

#ifdef PROBE_QEMU
	base = pci_ufs_probe();
	if (!base) {
		out("QEMU UFS not found\n");
		return;
	}
	kv("UFS_BAR", base);
#endif
	kv("UFS_CAP", rd32(base + 0x00));
	kv("UFS_VER", rd32(base + 0x08));
	kv("UFS_HCS", rd32(base + 0x30));
	out("\n");
#ifdef PROBE_QEMU
	ufs_qemu_read(base);
#endif
}

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
	if (ramfb_ready)
		halt();
	qemu_exit(1);
#endif
	halt();
}

void probe_main(uintptr_t dtb)
{
#ifdef PROBE_QEMU
	uart_init();
	ramfb_init();
#endif
	log_init();
	screen_init();
	out("E1S PROBE v1\n");
	out("Samsung Galaxy S24 (SM-S921B, e1s)\n");
	out("Exynos 2400 (S5E9945), 10-core ARMv9, Xclipse 940\n");
	out("8 GiB LPDDR5X, UFS 4.0, 1080x2340 120 Hz\n");
#ifdef PROBE_QEMU
	out("SIMULATED on QEMU virt - not official hardware\n");
	out("FDT/GIC/UFS values below are QEMU virt models\n");
#else
	out("SM-S921B hardware probe\n");
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
	ufs_dump();
	out("\nPROBE DONE\n");
	log_finish();
#ifdef PROBE_QEMU
	if (ramfb_ready) {
		out("QEMU display active (close the window to stop)\n");
		log_finish();
		halt();
	}
	out("QEMU EXIT\n");
	qemu_exit(0);
#endif
	out("halt\n");
	halt();
}
