/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "menu_ui.h"

#define W 1080
#define H 2340

static uint32_t pixels[W * H];

int main(int argc, char **argv)
{
	struct ui_fb fb = { pixels, W, H, W };
	struct menu_info info = {
		.kernel = "Linux 6.1.93 stock",
		.cmdline = "stock boot.img cmdline is empty",
		.load_base = 0x80000000ULL,
		.stock_base = 0x80200000ULL,
		.stock_size = 37640704,
		.stock_image_size = 40173568,
		.memory_size = 0x2850000ULL,
		.xnu_base = 0,
		.xnu_size = 0,
		.entry = 0x80200000ULL,
		.fb_base = 0xfa200000ULL,
		.fb_size = 0x9a4000ULL,
		.fb_w = W,
		.fb_h = H,
		.el = 1,
		.gic_ok = 1,
		.have_fb = 1,
		.menu = 1,
	};
	const char *names[3] = { "Android", "Recovery", "Download" };
	FILE *f;

	if (argc != 3) {
		fprintf(stderr, "usage: %s splash|auto|menu|recovery|download|error out.ppm\n",
		    argv[0]);
		return 1;
	}

	info.menu = strcmp(argv[1], "auto") != 0;
	menu_ui_begin(&fb, &info);
	if (!strcmp(argv[1], "splash"))
		menu_ui_splash(&fb, 5, 40, info.menu);
	else if (!strcmp(argv[1], "auto"))
		menu_ui_splash(&fb, 5, 40, 0);
	else if (!strcmp(argv[1], "menu"))
		menu_ui_options(&fb, names, 3, 0, 10, 40);
	else if (!strcmp(argv[1], "recovery"))
		menu_ui_message(&fb, "Recovery mode", "Hold Volume Up + Power.",
		    "Keep holding through the reset.", 3);
	else if (!strcmp(argv[1], "download"))
		menu_ui_message(&fb, "Download mode",
		    "Hold Volume Up + Volume Down.",
		    "Connect USB, release at the warning.", 3);
	else if (!strcmp(argv[1], "error"))
		menu_ui_error(&fb, "menu config is invalid");
	else
		return 2;

	f = fopen(argv[2], "wb");
	if (!f) {
		perror(argv[2]);
		return 1;
	}
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (int i = 0; i < W * H; i++) {
		uint32_t c = pixels[i];
		unsigned char rgb[3] = {
			(unsigned char)(c >> 16),
			(unsigned char)(c >> 8),
			(unsigned char)c,
		};
		fwrite(rgb, 1, sizeof(rgb), f);
	}
	if (fclose(f)) {
		perror(argv[2]);
		return 1;
	}
	return 0;
}
