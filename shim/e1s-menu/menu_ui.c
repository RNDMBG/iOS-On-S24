/* SPDX-License-Identifier: GPL-2.0-only */
#include "menu_ui.h"
#include "../e1s/font.h"

#define C_BG       0xff000000U
#define C_LINE     0xff303030U
#define C_BAR_BG   0xff101010U
#define C_BAR_EDGE 0xff505050U

#define MARGIN      56
#define VALUE_X     320
#define TEXT_SCALE  2
#define SMALL_SCALE 1
#define ROW_H       42

#define HDR_BRAND_Y   36
#define HDR_TITLE_Y   64
#define HDR_MODEL_Y   120
#define HDR_SUB_Y     154
#define HDR_LINE_Y    190

#define DEV_ROW_Y     220
#define BOOT_ROW_Y    588
#define KERNEL_ROW_Y  956

#define LOAD_TITLE_Y  1114
#define LOAD_BAR_Y    1154
#define LOAD_LABEL_Y  1194
#define LOAD_STATUS_Y 1234

#define STAGE_ROW_Y   1284
#define STAGE_ROW_H   36

#define COLOR_CAP_Y   1740
#define COLOR_BAR_Y   1772
#define COLOR_BAR_H   200
#define COLOR_LABEL_Y 1984
#define GREY_Y        2016
#define GREY_H        36
#define GREY_LABEL_Y  2060

#define FOOT_LINE_Y   2124
#define FOOT_TEXT_Y   2142

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
	if (w <= 0 || h <= 0)
		return;
	if ((uint32_t)x >= fb->width || (uint32_t)y >= fb->height)
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

static int slen(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	return n;
}

static void text_n(const struct ui_fb *fb, int x, int y, const char *s,
    int max, uint32_t color, int scale)
{
	for (int i = 0; i < max && s[i]; i++) {
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

static void text(const struct ui_fb *fb, int x, int y, const char *s,
    uint32_t color, int scale)
{
	text_n(fb, x, y, s, 1000, color, scale);
}

static void text_right(const struct ui_fb *fb, int right, int y,
    const char *s, uint32_t color, int scale)
{
	text(fb, right - slen(s) * 8 * scale, y, s, color, scale);
}

static char *put_str(char *p, const char *s)
{
	while ((*p = *s++))
		p++;
	return p;
}

static char *put_dec(char *p, uint64_t v)
{
	char tmp[20];
	int n = 0;

	if (!v)
		tmp[n++] = '0';
	while (v) {
		tmp[n++] = '0' + (v % 10);
		v /= 10;
	}
	while (n)
		*p++ = tmp[--n];
	*p = 0;
	return p;
}

static char *put_hex(char *p, uint64_t v)
{
	static const char d[] = "0123456789abcdef";
	char tmp[16];
	int n = 0;

	if (!v)
		tmp[n++] = '0';
	while (v) {
		tmp[n++] = d[v & 0xf];
		v >>= 4;
	}
	*p++ = '0';
	*p++ = 'x';
	while (n)
		*p++ = tmp[--n];
	*p = 0;
	return p;
}

static char *put_mb(char *p, uint64_t bytes)
{
	uint64_t tenths = (bytes * 10 + 500000) / 1000000;

	p = put_dec(p, tenths / 10);
	*p++ = '.';
	*p++ = '0' + (tenths % 10);
	*p = 0;
	return p;
}

static void row_label(const struct ui_fb *fb, int y, const char *label)
{
	text(fb, MARGIN, y, label, UI_TEXT_DIM, TEXT_SCALE);
}

static void row_value(const struct ui_fb *fb, int y, const char *value,
    uint32_t color)
{
	text(fb, VALUE_X, y, value, color, TEXT_SCALE);
}

static const char *const stage_names[] = {
	"S-Boot handoff",
	"Framebuffer",
	"GICv3",
	"Menu config",
	"Stock header",
	"Payload layout",
	"Cache clean",
	"DTB",
	"Jump to Android",
};

static const char *const device_labels[] = {
	"Model",
	"SoC",
	"CPU",
	"GPU",
	"Memory",
	"Storage",
	"Display",
	"Kernel",
};

static const char *const device_values[] = {
	"SM-S921B (e1s)",
	"Exynos 2400 (S5E9945)",
	"10-core ARMv9 (1+2+3+4)",
	"Xclipse 940",
	"8 GiB LPDDR5X",
	"UFS 4.0",
	"6.2-inch 1080x2340 120 Hz",
	"Android 14 stock",
};

static const char *const boot_labels[] = {
	"S-Boot",
	"EL",
	"GIC",
	"Framebuffer",
	"Payload span",
	"Stock kernel",
	"XNU loader",
	"Entry",
};

static const char *const kernel_labels[] = {
	"Stock base",
	"Image size",
	"Cmdline",
};

static void bar(const struct ui_fb *fb, int y, int percent)
{
	int x = MARGIN;
	int w = (int)fb->width - 2 * MARGIN;
	int fill;

	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	rect(fb, x, y, w, 28, C_BAR_EDGE);
	rect(fb, x + 2, y + 2, w - 4, 24, C_BAR_BG);
	fill = (w - 4) * percent / 100;
	if (fill > 0)
		rect(fb, x + 2, y + 2, fill, 24, UI_TEXT_MAIN);
}

static void loading(const struct ui_fb *fb, const char *title,
    const char *label, const char *status, int percent)
{
	char b[8];
	char *p = b;

	rect(fb, MARGIN, LOAD_TITLE_Y, (int)fb->width - 2 * MARGIN, 32, C_BG);
	text(fb, MARGIN, LOAD_TITLE_Y, title, UI_TEXT_MAIN, TEXT_SCALE);

	p = put_dec(p, (uint64_t)percent);
	p = put_str(p, "%");
	text_right(fb, (int)fb->width - MARGIN, LOAD_TITLE_Y, b, UI_TEXT_MAIN,
	    TEXT_SCALE);

	bar(fb, LOAD_BAR_Y, percent);
	rect(fb, MARGIN, LOAD_LABEL_Y, (int)fb->width - 2 * MARGIN, 32, C_BG);
	text_n(fb, MARGIN, LOAD_LABEL_Y, label, 61, UI_TEXT_MAIN, TEXT_SCALE);
	rect(fb, MARGIN, LOAD_STATUS_Y, (int)fb->width - 2 * MARGIN, 32, C_BG);
	text_n(fb, MARGIN, LOAD_STATUS_Y, status, 61, UI_TEXT_MUTED, TEXT_SCALE);
}

static void colour_bars(const struct ui_fb *fb)
{
	static const uint32_t bars[8] = {
		0xffffffffU, 0xffffff00U, 0xff00ffffU, 0xff00ff00U,
		0xffff00ffU, 0xffff0000U, 0xff0000ffU, 0xff000000U,
	};
	static const char *const names[8] = {
		"WHITE", "YELLOW", "CYAN", "GREEN",
		"MAGENTA", "RED", "BLUE", "BLACK",
	};
	int inner_x = MARGIN + 2;
	int inner_w = (int)fb->width - 2 * MARGIN - 4;

	text(fb, MARGIN, COLOR_CAP_Y, "panel colour test", UI_TEXT_MUTED,
	    SMALL_SCALE);

	rect(fb, MARGIN, COLOR_BAR_Y, (int)fb->width - 2 * MARGIN,
	    COLOR_BAR_H, C_BAR_EDGE);
	for (int i = 0; i < 8; i++) {
		int x0 = inner_x + inner_w * i / 8;
		int x1 = inner_x + inner_w * (i + 1) / 8;
		int lx = (x0 + x1) / 2 - slen(names[i]) * 8 / 2;

		rect(fb, x0, COLOR_BAR_Y + 2, x1 - x0, COLOR_BAR_H - 4,
		    bars[i]);
		text(fb, lx, COLOR_LABEL_Y, names[i], UI_TEXT_DIM,
		    SMALL_SCALE);
	}

	rect(fb, MARGIN, GREY_Y, (int)fb->width - 2 * MARGIN, GREY_H,
	    C_BAR_EDGE);
	for (int i = 0; i < 16; i++) {
		int v = i * 255 / 15;
		uint32_t c = 0xff000000U | ((uint32_t)v << 16) |
		    ((uint32_t)v << 8) | (uint32_t)v;
		int x0 = inner_x + inner_w * i / 16;
		int x1 = inner_x + inner_w * (i + 1) / 16;

		rect(fb, x0, GREY_Y + 2, x1 - x0, GREY_H - 4, c);
	}
	text(fb, MARGIN, GREY_LABEL_Y, "0", UI_TEXT_MUTED, SMALL_SCALE);
	text_right(fb, (int)fb->width - MARGIN, GREY_LABEL_Y, "255",
	    UI_TEXT_MUTED, SMALL_SCALE);
}

static void build_fb_row(char *b, const struct menu_info *info)
{
	char *p = b;

	if (!info->have_fb) {
		p = put_str(p, "not found - fallback ");
		p = put_hex(p, info->fb_base);
		return;
	}
	p = put_hex(p, info->fb_base);
	p = put_str(p, " ");
	p = put_dec(p, info->fb_w);
	p = put_str(p, "x");
	p = put_dec(p, info->fb_h);
	p = put_str(p, " 32bpp DTB");
}

static void build_stock_row(char *b, const struct menu_info *info)
{
	char *p = b;

	p = put_mb(p, info->stock_size);
	p = put_str(p, " MB at +");
	p = put_hex(p, info->stock_base - info->load_base);
}

static void build_xnu_row(char *b, const struct menu_info *info)
{
	char *p = b;

	if (!info->xnu_size) {
		put_str(p, "not included");
		return;
	}
	p = put_mb(p, info->xnu_size);
	p = put_str(p, " MB at +");
	put_hex(p, info->xnu_base - info->load_base);
}

static void build_image_row(char *b, const struct menu_info *info)
{
	char *p = b;

	p = put_hex(p, info->stock_image_size);
	p = put_str(p, "  ");
	p = put_mb(p, info->stock_image_size);
	p = put_str(p, " MB");
}

static void stage_row(const struct ui_fb *fb, int stage, const char *mark,
    uint32_t mark_color, uint32_t name_color)
{
	int y = STAGE_ROW_Y + stage * STAGE_ROW_H;

	rect(fb, 0, y, (int)fb->width, STAGE_ROW_H, C_BG);
	text(fb, MARGIN, y, mark, mark_color, TEXT_SCALE);
	text(fb, 160, y, stage_names[stage], name_color, TEXT_SCALE);
}

static void stages_done(const struct ui_fb *fb, int jumped)
{
	for (int i = 0; i < 9; i++) {
		if (i < 8 || jumped)
			stage_row(fb, i, "ok", UI_TEXT_OK, UI_TEXT_MAIN);
		else
			stage_row(fb, i, "wait", UI_TEXT_MUTED, UI_TEXT_MUTED);
	}
}

void menu_ui_begin(const struct ui_fb *fb, const struct menu_info *info)
{
	char b[200];

	rect(fb, 0, 0, (int)fb->width, (int)fb->height, C_BG);

	text(fb, MARGIN, HDR_BRAND_Y, "SAMSUNG", UI_TEXT_MUTED, SMALL_SCALE);
	text(fb, MARGIN, HDR_TITLE_Y, "Galaxy S24", UI_TEXT_MAIN, 3);
	text(fb, MARGIN, HDR_MODEL_Y, "SM-S921B  |  e1s  |  Exynos 2400",
	    UI_TEXT_DIM, TEXT_SCALE);
	text(fb, MARGIN, HDR_SUB_Y,
	    "stock Linux kernel  |  e1s boot screen  |  experimental",
	    UI_TEXT_MUTED, SMALL_SCALE);

	rect(fb, MARGIN, HDR_LINE_Y, (int)fb->width - 2 * MARGIN, 2, C_LINE);

	for (int i = 0; i < 8; i++) {
		row_label(fb, DEV_ROW_Y + i * ROW_H, device_labels[i]);
		row_value(fb, DEV_ROW_Y + i * ROW_H,
		    i == 7 && info->kernel ? info->kernel : device_values[i],
		    UI_TEXT_MAIN);
	}

	for (int i = 0; i < 8; i++)
		row_label(fb, BOOT_ROW_Y + i * ROW_H, boot_labels[i]);
	row_value(fb, BOOT_ROW_Y, "entered loader", UI_TEXT_OK);

	put_str(b, "EL");
	put_dec(b + slen(b), info->el);
	row_value(fb, BOOT_ROW_Y + ROW_H, b, UI_TEXT_OK);

	row_value(fb, BOOT_ROW_Y + 2 * ROW_H,
	    info->gic_ok ? "GICv3 present" : "read failed",
	    info->gic_ok ? UI_TEXT_OK : UI_TEXT_WARN);

	build_fb_row(b, info);
	row_value(fb, BOOT_ROW_Y + 3 * ROW_H, b,
	    info->have_fb ? UI_TEXT_MAIN : UI_TEXT_WARN);

	put_hex(b, info->load_base);
	put_str(b + slen(b), " + ");
	put_hex(b + slen(b), info->memory_size);
	row_value(fb, BOOT_ROW_Y + 4 * ROW_H, b, UI_TEXT_MAIN);

	build_stock_row(b, info);
	row_value(fb, BOOT_ROW_Y + 5 * ROW_H, b, UI_TEXT_MAIN);

	build_xnu_row(b, info);
	row_value(fb, BOOT_ROW_Y + 6 * ROW_H, b,
	    info->xnu_size ? UI_TEXT_MAIN : UI_TEXT_MUTED);

	put_hex(b, info->entry);
	row_value(fb, BOOT_ROW_Y + 7 * ROW_H, b, UI_TEXT_MAIN);

	for (int i = 0; i < 3; i++)
		row_label(fb, KERNEL_ROW_Y + i * ROW_H, kernel_labels[i]);

	put_hex(b, info->stock_base);
	row_value(fb, KERNEL_ROW_Y, b, UI_TEXT_MAIN);

	build_image_row(b, info);
	row_value(fb, KERNEL_ROW_Y + ROW_H, b, UI_TEXT_MAIN);

	text_n(fb, VALUE_X, KERNEL_ROW_Y + 2 * ROW_H,
	    info->cmdline ? info->cmdline : "(empty)", 42, UI_TEXT_DIM,
	    TEXT_SCALE);

	loading(fb, "Loading Android", "waiting for the countdown",
	    info->menu ? "Volume Up: boot menu" : "auto-boot: no key menu", 0);
	stages_done(fb, 0);

	colour_bars(fb);

	rect(fb, MARGIN, FOOT_LINE_Y, (int)fb->width - 2 * MARGIN, 2, C_LINE);
	text(fb, MARGIN, FOOT_TEXT_Y,
	    "e1s boot screen  |  experimental BOOT-only test image",
	    UI_TEXT_MUTED, SMALL_SCALE);
	text_right(fb, (int)fb->width - MARGIN, FOOT_TEXT_Y,
	    "not official firmware", UI_TEXT_MUTED, SMALL_SCALE);
}

void menu_ui_splash(const struct ui_fb *fb, int seconds, int percent,
    int menu)
{
	char b[48];
	char *p = b;

	p = put_str(p, "Android in ");
	p = put_dec(p, (uint32_t)seconds);
	p = put_str(p, " s");
	loading(fb, "Loading Android", b,
	    menu ? "Volume Up: boot menu" : "auto-boot: no key menu", percent);
	stages_done(fb, seconds <= 0);
}

void menu_ui_options(const struct ui_fb *fb, const char *const *names,
    int count, int selected, int seconds, int percent)
{
	char b[48];
	char *p;

	p = put_str(b, "auto-boot Android in ");
	p = put_dec(p, (uint32_t)seconds);
	p = put_str(p, " s");
	loading(fb, "Boot menu", b, "Volume Up: next | hold: select", percent);

	rect(fb, 0, STAGE_ROW_Y, (int)fb->width, 1608 - STAGE_ROW_Y, C_BG);
	for (int i = 0; i < count && i < 4; i++) {
		int y = STAGE_ROW_Y + i * 56;
		uint32_t color = i == selected ? UI_TEXT_MAIN : UI_TEXT_MUTED;

		text(fb, MARGIN, y, i == selected ? ">" : " ", color,
		    TEXT_SCALE);
		text(fb, MARGIN + 32, y, names[i], color, TEXT_SCALE);
	}
	text(fb, MARGIN, 1524, "Android boots if the menu times out.",
	    UI_TEXT_MUTED, SMALL_SCALE);
}

void menu_ui_message(const struct ui_fb *fb, const char *title,
    const char *line1, const char *line2, int seconds)
{
	char b[48];
	char *p;

	rect(fb, 0, LOAD_TITLE_Y, (int)fb->width, 1608 - LOAD_TITLE_Y, C_BG);
	text(fb, MARGIN, LOAD_TITLE_Y, title, UI_TEXT_MAIN, 3);
	text_n(fb, MARGIN, 1178, line1, 60, UI_TEXT_MAIN, TEXT_SCALE);
	text_n(fb, MARGIN, 1218, line2, 60, UI_TEXT_DIM, TEXT_SCALE);

	p = put_str(b, "resetting in ");
	p = put_dec(p, (uint32_t)seconds);
	p = put_str(p, " s");
	text(fb, MARGIN, 1320, b, UI_TEXT_WARN, TEXT_SCALE);
	text(fb, MARGIN, 1380, "The keys must be held through the reset.",
	    UI_TEXT_MUTED, SMALL_SCALE);
}

void menu_ui_error(const struct ui_fb *fb, const char *why)
{
	rect(fb, 0, LOAD_TITLE_Y, (int)fb->width, 1608 - LOAD_TITLE_Y, C_BG);
	text(fb, MARGIN, LOAD_TITLE_Y, "BOOT FAILED", UI_TEXT_ERR, 3);
	text_n(fb, MARGIN, 1178, why, 60, UI_TEXT_MAIN, TEXT_SCALE);
	text(fb, MARGIN, 1218, "halted - see the 0x91200000 log",
	    UI_TEXT_DIM, TEXT_SCALE);
}
