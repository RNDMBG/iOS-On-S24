/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef E1S_UI_H
#define E1S_UI_H

#include <stdint.h>

#define UI_TEXT_MAIN  0xffffffffU
#define UI_TEXT_DIM   0xff9ca3afU
#define UI_TEXT_MUTED 0xff6b7280U
#define UI_TEXT_OK    0xff34d399U
#define UI_TEXT_WARN  0xfffbbf24U
#define UI_TEXT_ERR   0xfff87171U

struct ui_fb {
	volatile uint32_t *base;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
};

struct ui_boot_info {
	int have_fb;
	uint64_t fb_base;
	uint64_t fb_size;
	uint32_t fb_w;
	uint32_t fb_h;
	uint32_t fb_stride;
	uint64_t el;
	int gic_ok;
	uint64_t virt_base;
	uint64_t entry_va;
	uint64_t phys_base;
	uint64_t mem_size;
	uint64_t win_base;
	uint64_t win_size;
	uint64_t payload_size;
	uint32_t nsegs;
	uint32_t adt_size;
	const char *cmdline;
};

enum {
	UI_STAGE_SBOOT,
	UI_STAGE_FB,
	UI_STAGE_GIC,
	UI_STAGE_PAYLOAD,
	UI_STAGE_RAM,
	UI_STAGE_CLEAR,
	UI_STAGE_COPY,
	UI_STAGE_ADT,
	UI_STAGE_JUMP,
	UI_STAGE_COUNT
};

enum {
	UI_PENDING,
	UI_ACTIVE,
	UI_OK,
	UI_WARN,
	UI_FAIL
};

void ui_begin(const struct ui_fb *fb);
void ui_set_info(const struct ui_fb *fb, const struct ui_boot_info *info);
void ui_set_stage(const struct ui_fb *fb, int stage, int state);
void ui_set_progress(const struct ui_fb *fb, int percent, const char *label);
void ui_set_status(const struct ui_fb *fb, const char *text, uint32_t color);
void ui_fail(const struct ui_fb *fb, const char *why);

char *ui_put_str(char *p, const char *s);
char *ui_put_dec(char *p, uint64_t v);
char *ui_put_hex(char *p, uint64_t v);
char *ui_put_mb(char *p, uint64_t bytes);

#endif
