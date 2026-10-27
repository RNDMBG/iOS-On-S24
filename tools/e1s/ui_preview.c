/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdint.h>
#include "ui.h"

#define W 1080
#define H 2340

static uint32_t pixels[W * H];

int main(int argc, char **argv)
{
	struct ui_fb fb = { pixels, W, H, W };
	struct ui_boot_info info = {
		.have_fb = 1,
		.fb_base = 0xfa200000,
		.fb_size = 0x009a4000,
		.fb_w = 1080,
		.fb_h = 2340,
		.fb_stride = 4320,
		.el = 1,
		.gic_ok = 1,
		.virt_base = 0xfffffff004000000ULL,
		.entry_va = 0xfffffff0074264a8ULL,
		.phys_base = 0xa0000000ULL,
		.mem_size = 0x20000000ULL,
		.win_base = 0xa0000000ULL,
		.win_size = 0x20000000ULL,
		.payload_size = 16262016,
		.nsegs = 10,
		.adt_size = 16644,
		.cmdline = "debug=0x14e cpus=1 keepsyms=1 -v",
	};
	FILE *f;

	if (argc != 2) {
		fprintf(stderr, "usage: %s out.ppm\n", argv[0]);
		return 1;
	}
	ui_begin(&fb);
	for (int i = 0; i < UI_STAGE_COUNT; i++)
		ui_set_stage(&fb, i, UI_OK);
	ui_set_info(&fb, &info);
	ui_set_progress(&fb, 100, "Kernel ready");
	ui_set_status(&fb, "boot_args ready - jumping to XNU", UI_TEXT_OK);

	f = fopen(argv[1], "wb");
	if (!f) {
		perror(argv[1]);
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
		perror(argv[1]);
		return 1;
	}
	return 0;
}
