/* SPDX-License-Identifier: GPL-2.0-only */
#include "ui.h"
#include "../e1s/font.h"

#define C_BG       0xff000000U
#define C_ACCENT   0xff4da3ffU
#define C_LINE     0xff243041U
#define C_BAR_BG   0xff111827U
#define C_BAR_EDGE 0xff374151U

#define MARGIN      48
#define VALUE_X     360
#define TEXT_SCALE  2
#define SMALL_SCALE 1
#define ROW_H       40

#define HDR_TITLE_Y  40
#define HDR_SUB_Y    128
#define HDR_SUB2_Y   168
#define HDR_LINE_Y   204
#define DEV_SEC_Y    232
#define DEV_ROW_Y    288
#define BOOT_SEC_Y   632
#define BOOT_ROW_Y   688
#define KIMG_SEC_Y   1040
#define KIMG_ROW_Y   1096
#define LOAD_SEC_Y   1240
#define LOAD_BAR_Y   1300
#define LOAD_LABEL_Y 1340
#define LOAD_STATUS_Y 1380
#define STAGE_SEC_Y  1450
#define STAGE_ROW_Y  1506
#define STAGE_ROW_H  36
#define COLOR_CAP_Y  1866
#define COLOR_BAR_Y  1910
#define COLOR_BAR_H  206
#define COLOR_LABEL_Y 2126
#define GREY_Y       2160
#define GREY_H       40
#define GREY_LABEL_Y 2208
#define FOOT_LINE_Y  2242
#define FOOT_TEXT_Y  2258
#define BOTTOM_BAR_Y 2332

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

char *ui_put_str(char *p, const char *s)
{
	while ((*p = *s++))
		p++;
	return p;
}

char *ui_put_dec(char *p, uint64_t v)
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

char *ui_put_hex(char *p, uint64_t v)
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

char *ui_put_mb(char *p, uint64_t bytes)
{
	uint64_t tenths = (bytes * 10 + 500000) / 1000000;

	p = ui_put_dec(p, tenths / 10);
	*p++ = '.';
	*p++ = '0' + (tenths % 10);
	*p = 0;
	return p;
}

static void section(const struct ui_fb *fb, int y, const char *title)
{
	rect(fb, MARGIN, y + 6, 8, 20, C_ACCENT);
	text(fb, MARGIN + 20, y, title, C_ACCENT, TEXT_SCALE);
	rect(fb, MARGIN, y + 38, (int)fb->width - 2 * MARGIN, 2, C_LINE);
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

static const char *const stage_names[UI_STAGE_COUNT] = {
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
	"XNU 8796.121.2 (iOS 16.5)",
};

static const char *const boot_labels[] = {
	"S-Boot",
	"Exception",
	"GIC",
	"Framebuffer",
	"RAM window",
	"Payload",
	"ADT",
	"Entry",
};

static const char *const kernel_labels[] = {
	"Virt base",
	"Phys load",
	"Cmdline",
};

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

	text(fb, MARGIN, COLOR_CAP_Y, "DISPLAY COLOUR TEST", C_ACCENT,
	    TEXT_SCALE);
	text_right(fb, (int)fb->width - MARGIN, COLOR_CAP_Y + 6,
	    "8 BARS + 16-STEP GREY RAMP", UI_TEXT_MUTED, SMALL_SCALE);

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

void ui_begin(const struct ui_fb *fb)
{
	rect(fb, 0, 0, (int)fb->width, (int)fb->height, C_BG);
	rect(fb, 0, 0, (int)fb->width, 8, C_ACCENT);

	text(fb, MARGIN, HDR_TITLE_Y, "Galaxy S24", UI_TEXT_MAIN, 4);
	text(fb, MARGIN, HDR_SUB_Y, "SM-S921B  |  e1s  |  Exynos 2400",
	    UI_TEXT_DIM, TEXT_SCALE);
	text(fb, MARGIN, HDR_SUB2_Y, "XNU 8796.121.2  |  BOOT-only bring-up",
	    UI_TEXT_MUTED, TEXT_SCALE);

	rect(fb, 808, 48, 224, 88, C_BG);
	rect(fb, 808, 48, 224, 2, UI_TEXT_WARN);
	rect(fb, 808, 134, 224, 2, UI_TEXT_WARN);
	rect(fb, 808, 48, 2, 88, UI_TEXT_WARN);
	rect(fb, 1030, 48, 2, 88, UI_TEXT_WARN);
	text(fb, 824, 64, "EXPERIMENTAL", UI_TEXT_WARN, TEXT_SCALE);
	text(fb, 824, 104, "e1s loader", UI_TEXT_MUTED, TEXT_SCALE);

	rect(fb, MARGIN, HDR_LINE_Y, (int)fb->width - 2 * MARGIN, 2, C_LINE);

	section(fb, DEV_SEC_Y, "DEVICE");
	for (int i = 0; i < 8; i++) {
		row_label(fb, DEV_ROW_Y + i * ROW_H, device_labels[i]);
		row_value(fb, DEV_ROW_Y + i * ROW_H, device_values[i],
		    UI_TEXT_MAIN);
	}

	section(fb, BOOT_SEC_Y, "BOOT HANDOFF");
	for (int i = 0; i < 8; i++)
		row_label(fb, BOOT_ROW_Y + i * ROW_H, boot_labels[i]);

	section(fb, KIMG_SEC_Y, "KERNEL IMAGE");
	for (int i = 0; i < 3; i++)
		row_label(fb, KIMG_ROW_Y + i * ROW_H, kernel_labels[i]);

	section(fb, LOAD_SEC_Y, "KERNEL LOAD");
	ui_set_progress(fb, 0, "Waiting for boot data");
	ui_set_status(fb, "", UI_TEXT_MUTED);

	section(fb, STAGE_SEC_Y, "BOOT SEQUENCE");
	for (int i = 0; i < UI_STAGE_COUNT; i++)
		ui_set_stage(fb, i, UI_PENDING);

	colour_bars(fb);

	rect(fb, MARGIN, FOOT_LINE_Y, (int)fb->width - 2 * MARGIN, 2, C_LINE);
	text(fb, MARGIN, FOOT_TEXT_Y,
	    "e1s bring-up loader  |  experimental BOOT-only test image",
	    UI_TEXT_MUTED, SMALL_SCALE);
	text_right(fb, (int)fb->width - MARGIN, FOOT_TEXT_Y,
	    "not official firmware", UI_TEXT_MUTED, SMALL_SCALE);
	rect(fb, 0, BOTTOM_BAR_Y, (int)fb->width, 8, C_ACCENT);
}

void ui_set_info(const struct ui_fb *fb, const struct ui_boot_info *info)
{
	char b[200];
	char *p;

	p = b;
	p = ui_put_str(p, "accepted boot.img");
	row_value(fb, BOOT_ROW_Y, b, UI_TEXT_OK);

	p = b;
	p = ui_put_str(p, "EL");
	p = ui_put_dec(p, info->el);
	row_value(fb, BOOT_ROW_Y + ROW_H, b,
	    info->el == 1 ? UI_TEXT_OK : UI_TEXT_ERR);

	row_value(fb, BOOT_ROW_Y + 2 * ROW_H,
	    info->gic_ok ? "GICv3 ready" : "not ready",
	    info->gic_ok ? UI_TEXT_OK : UI_TEXT_WARN);

	p = b;
	if (info->have_fb) {
		p = ui_put_hex(p, info->fb_base);
		p = ui_put_str(p, "  ");
		p = ui_put_dec(p, info->fb_w);
		p = ui_put_str(p, "x");
		p = ui_put_dec(p, info->fb_h);
		p = ui_put_str(p, "  32bpp");
		row_value(fb, BOOT_ROW_Y + 3 * ROW_H, b, UI_TEXT_MAIN);
	} else {
		p = ui_put_str(p, "not found - fallback ");
		p = ui_put_hex(p, info->fb_base);
		row_value(fb, BOOT_ROW_Y + 3 * ROW_H, b, UI_TEXT_WARN);
	}

	p = b;
	p = ui_put_hex(p, info->win_base);
	p = ui_put_str(p, " + ");
	p = ui_put_hex(p, info->win_size);
	row_value(fb, BOOT_ROW_Y + 4 * ROW_H, b, UI_TEXT_MAIN);

	p = b;
	p = ui_put_dec(p, info->nsegs);
	p = ui_put_str(p, " segments  ");
	p = ui_put_mb(p, info->payload_size);
	p = ui_put_str(p, " MB");
	row_value(fb, BOOT_ROW_Y + 5 * ROW_H, b, UI_TEXT_MAIN);

	p = b;
	p = ui_put_dec(p, info->adt_size);
	p = ui_put_str(p, " bytes");
	row_value(fb, BOOT_ROW_Y + 6 * ROW_H, b, UI_TEXT_MAIN);

	p = b;
	p = ui_put_hex(p, info->entry_va);
	row_value(fb, BOOT_ROW_Y + 7 * ROW_H, b, UI_TEXT_MAIN);

	p = b;
	p = ui_put_hex(p, info->virt_base);
	row_value(fb, KIMG_ROW_Y, b, UI_TEXT_MAIN);

	p = b;
	p = ui_put_hex(p, info->phys_base);
	row_value(fb, KIMG_ROW_Y + ROW_H, b, UI_TEXT_MAIN);

	if (info->cmdline)
		text_n(fb, VALUE_X, KIMG_ROW_Y + 2 * ROW_H,
		    info->cmdline, 42, UI_TEXT_DIM, TEXT_SCALE);
}

void ui_set_stage(const struct ui_fb *fb, int stage, int state)
{
	const char *mark;
	uint32_t mark_color;
	uint32_t name_color;
	int y;

	if (stage < 0 || stage >= UI_STAGE_COUNT)
		return;
	y = STAGE_ROW_Y + stage * STAGE_ROW_H;
	rect(fb, 0, y, (int)fb->width, STAGE_ROW_H, C_BG);
	rect(fb, 32, y + 4, 4, 24, state == UI_ACTIVE ? C_ACCENT : C_BG);

	switch (state) {
	case UI_ACTIVE:
		mark = ">>";
		mark_color = UI_TEXT_WARN;
		name_color = UI_TEXT_MAIN;
		break;
	case UI_OK:
		mark = "OK";
		mark_color = UI_TEXT_OK;
		name_color = UI_TEXT_MAIN;
		break;
	case UI_WARN:
		mark = "??";
		mark_color = UI_TEXT_WARN;
		name_color = UI_TEXT_WARN;
		break;
	case UI_FAIL:
		mark = "!!";
		mark_color = UI_TEXT_ERR;
		name_color = UI_TEXT_ERR;
		break;
	default:
		mark = "..";
		mark_color = UI_TEXT_MUTED;
		name_color = UI_TEXT_MUTED;
		break;
	}
	text(fb, MARGIN, y, mark, mark_color, TEXT_SCALE);
	text(fb, 160, y, stage_names[stage], name_color, TEXT_SCALE);
}

void ui_set_progress(const struct ui_fb *fb, int percent, const char *label)
{
	char b[8];
	char *p = b;
	int bar_w = (int)fb->width - 2 * MARGIN;
	int fill;

	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	p = ui_put_dec(p, (uint64_t)percent);
	p = ui_put_str(p, "%");

	rect(fb, 940, LOAD_SEC_Y, 92, 32, C_BG);
	text_right(fb, (int)fb->width - MARGIN, LOAD_SEC_Y, b, UI_TEXT_MAIN,
	    TEXT_SCALE);

	rect(fb, MARGIN, LOAD_BAR_Y, bar_w, 28, C_BAR_EDGE);
	rect(fb, MARGIN + 2, LOAD_BAR_Y + 2, bar_w - 4, 24, C_BAR_BG);
	fill = (bar_w - 4) * percent / 100;
	if (fill > 0)
		rect(fb, MARGIN + 2, LOAD_BAR_Y + 2, fill, 24, C_ACCENT);

	rect(fb, MARGIN, LOAD_LABEL_Y, bar_w, 32, C_BG);
	text_n(fb, MARGIN, LOAD_LABEL_Y, label, 61, UI_TEXT_MAIN, TEXT_SCALE);
}

void ui_set_status(const struct ui_fb *fb, const char *text, uint32_t color)
{
	rect(fb, MARGIN, LOAD_STATUS_Y, (int)fb->width - 2 * MARGIN, 32, C_BG);
	text_n(fb, MARGIN, LOAD_STATUS_Y, text, 61, color, TEXT_SCALE);
}

void ui_fail(const struct ui_fb *fb, const char *why)
{
	int x = MARGIN;
	int y = LOAD_SEC_Y - 8;
	int w = (int)fb->width - 2 * MARGIN;
	int h = 170;

	rect(fb, x, y, w, h, UI_TEXT_ERR);
	rect(fb, x + 2, y + 2, w - 4, h - 4, 0xff2a0b0b);
	text(fb, x + 24, y + 18, "BOOT FAILED", UI_TEXT_MAIN, TEXT_SCALE);
	text_n(fb, x + 24, y + 58, why, 52, UI_TEXT_ERR, TEXT_SCALE);
	text(fb, x + 24, y + 98, "halted - see the 0x91200000 log",
	    UI_TEXT_DIM, TEXT_SCALE);
}
