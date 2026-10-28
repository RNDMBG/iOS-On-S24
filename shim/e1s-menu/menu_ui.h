#ifndef E1S_MENU_UI_H
#define E1S_MENU_UI_H

#include <stdint.h>
#include "../e1s-xnu/ui.h"

struct menu_info {
	const char *kernel;
	const char *cmdline;
	uint64_t load_base;
	uint64_t stock_base;
	uint64_t stock_size;
	uint64_t stock_image_size;
	uint64_t memory_size;
	uint64_t xnu_base;
	uint64_t xnu_size;
	uint64_t entry;
	uint64_t fb_base;
	uint64_t fb_size;
	uint32_t fb_w;
	uint32_t fb_h;
	uint32_t el;
	int gic_ok;
	int have_fb;
	int menu;
};

void menu_ui_begin(const struct ui_fb *fb, const struct menu_info *info);
void menu_ui_splash(const struct ui_fb *fb, int seconds, int percent,
    int menu);
void menu_ui_options(const struct ui_fb *fb, const char *const *names,
    int count, int selected, int seconds, int percent);
void menu_ui_message(const struct ui_fb *fb, const char *title,
    const char *line1, const char *line2, int seconds);
void menu_ui_error(const struct ui_fb *fb, const char *why);

#endif
