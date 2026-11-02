/* Render the e1s loader flow to raw BGRA frames on stdout.
 *
 * The loader frames call the real shim/e1s-xnu/ui.c with the values from
 * out/qemu-screen/bootinfo.h (generated from a built image by
 * qemu_bootinfo.py). The console frames are the expected log, not a capture.
 *
 * usage: render_boot > frames.bgra
 *        gcc -O2 -I out/qemu-screen -I shim/e1s-xnu -I shim/e1s \
 *            tools/e1s/render_boot.c shim/e1s-xnu/ui.c -o render_boot
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "font.h"
#include "bootinfo.h"

#define W 1080
#define H 2340
#define FPS 30
#define LOAD_FRAMES 210
#define HOLD_FRAMES 45
#define GAP_FRAMES 6
#define LINE_FRAMES 4
#define TAIL_FRAMES 60

static uint32_t fb[W * H];
static struct ui_fb scr = { fb, W, H, W };

static const struct ui_boot_info info = {
	.have_fb = 1,
	.fb_base = 0xFA200000ULL,
	.fb_size = 0x9A4000ULL,
	.fb_w = W,
	.fb_h = H,
	.fb_stride = W * 4,
	.el = 1,
	.gic_ok = 1,
	.virt_base = BI_VIRT_BASE,
	.entry_va = BI_ENTRY,
	.phys_base = BI_PHYS_BASE,
	.mem_size = BI_MEM_SIZE,
	.win_base = BI_PHYS_BASE,
	.win_size = BI_MEM_SIZE,
	.payload_size = BI_PAYLOAD_BYTES,
	.nsegs = BI_NSEGS,
	.adt_size = BI_ADT_BYTES,
	.cmdline = BI_CMDLINE,
};

static const char *const step_labels[UI_STAGE_COUNT] = {
	"S-Boot handoff", "Framebuffer", "GICv3", "Payload header",
	"RAM window", "Clear XNU RAM", "Copy kernel segments",
	"ADT + boot_args", "Jump to XNU",
};

static const char *const console_lines[] = {
	"e1s-log: console ring at physical 0x91300000, 1024 KiB; dump 2 MiB at 0x91200000",
	"e1s-display: framebuffer 0xfa200000 1080x2340 stride 4320 depth 32",
	"e1s-acpm: mailbox 0x12b90000, channel 2, firmware table at 0x0205ed00",
	"e1s-pmic: S2MPS27 0x0d/0x1d, S2MPS28-4 0x0b/0x5f: read-only",
	"e1s-battery: MAX77775 at 0x36: status, soc, cell, current, temp",
	"e1s-ufs: HCI 0x17100000, MCQ queue 0, read-only",
	"apfs: volume 0 \"ROOT\": 2 files, 5 directories, 5 blocks of data",
	"BSD root: md0, major 3, minor 0",
	"load_init_program: attempting to load /sbin/launchd",
	"*** SWEET userspace: init running as PID 1 ***",
	"SWEET init: /dev/md1 mounted",
	"SWEET init: still alive",
};

static void rect(int x, int y, int w, int h, uint32_t color)
{
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (w <= 0 || h <= 0 || x >= W || y >= H)
		return;
	if (x + w > W)
		w = W - x;
	if (y + h > H)
		h = H - y;
	for (int yy = 0; yy < h; yy++) {
		uint32_t *row = fb + (y + yy) * W;

		for (int xx = 0; xx < w; xx++)
			row[x + xx] = color;
	}
}

static void text(int x, int y, const char *s, uint32_t color, int scale)
{
	for (int i = 0; s[i]; i++) {
		unsigned char ch = (unsigned char)s[i];
		const unsigned char *g;

		if (ch < 0x20 || ch > 0x7e)
			ch = '?';
		g = font8x16[ch - 0x20];
		for (int gy = 0; gy < 16; gy++)
			for (int gx = 0; gx < 8; gx++)
				if (g[gy] & (0x80 >> gx))
					rect(x + i * 8 * scale + gx * scale,
					    y + gy * scale, scale, scale, color);
	}
}

static void emit(void)
{
	fwrite(fb, sizeof(fb), 1, stdout);
}

static void loader_begin(void)
{
	ui_begin(&scr);
	ui_set_info(&scr, &info);
	ui_set_progress(&scr, 0, "Waiting for boot data");
	ui_set_status(&scr, "", UI_TEXT_MUTED);
}

static void loader_done(void)
{
	for (int i = 0; i < UI_STAGE_COUNT; i++)
		ui_set_stage(&scr, i, UI_OK);
	ui_set_progress(&scr, 100, "Kernel ready");
	ui_set_status(&scr, "boot_args ready - jumping to XNU", UI_TEXT_OK);
}

static void console_frame(int revealed, int cursor_on)
{
	int y = 104;

	rect(0, 0, W, H, 0xff000000U);
	text(40, 28, "XNU 8796.121.2 (iOS 16.5) - e1s", 0xffffffffU, 1);
	text(40, 56, "boot_args: " BI_CMDLINE, 0xff9ca3afU, 1);

	for (int i = 0; i < revealed; i++) {
		text(40, y, console_lines[i], 0xffe5e7ebU, 1);
		y += 26;
	}
	if (cursor_on)
		text(40, y, "_", 0xffe5e7ebU, 1);

	/* One line, bottom left. This render is not a hardware capture. */
	text(40, H - 56,
	    "expected console from the patched kernel - not captured from a phone",
	    0xff4b5563U, 1);
}

int main(void)
{
	int active = -1;

	loader_begin();
	for (int f = 0; f < LOAD_FRAMES; f++) {
		double t = (double)f / (LOAD_FRAMES - 1);
		int percent = (int)(t * 100.0 + 0.5);
		int stage = percent * UI_STAGE_COUNT / 100;
		char label[64];

		if (stage > UI_STAGE_COUNT - 1)
			stage = UI_STAGE_COUNT - 1;
		if (stage != active) {
			if (active >= 0)
				ui_set_stage(&scr, active, UI_OK);
			ui_set_stage(&scr, stage, UI_ACTIVE);
			active = stage;
		}
		snprintf(label, sizeof(label), "%d%%  %s", percent,
		    step_labels[stage]);
		ui_set_progress(&scr, percent, label);
		emit();
	}
	loader_done();
	for (int f = 0; f < HOLD_FRAMES; f++)
		emit();

	for (int f = 0; f < GAP_FRAMES; f++) {
		rect(0, 0, W, H, 0xff000000U);
		emit();
	}

	int total = (int)(sizeof(console_lines) / sizeof(console_lines[0]));
	int frames = (total + 2) * LINE_FRAMES + TAIL_FRAMES;

	for (int f = 0; f < frames; f++) {
		int revealed = f / LINE_FRAMES;

		if (revealed > total + 1)
			revealed = total + 1;
		console_frame(revealed > total ? total : revealed,
		    (f / (FPS / 4)) & 1);
		emit();
	}
	return 0;
}
