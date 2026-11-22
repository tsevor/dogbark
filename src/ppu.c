#include "ppu.h"
#include "host.h"
#include <string.h>
#include <stdlib.h>

static const uint8_t OBJ_SIZE[4][3][2] = {
	// square      horizontal  vertical
	{ {  8,  8 }, { 16,  8 }, {  8, 16 } }, // 0
	{ { 16, 16 }, { 32,  8 }, {  8, 32 } }, // 1
	{ { 32, 32 }, { 32, 16 }, { 16, 32 } }, // 2
	{ { 64, 64 }, { 64, 32 }, { 32, 64 } }, // 3
};

static const uint8_t BG_TYPES[6][4] = {
	{ 1, 1, 1, 1 }, // 0 = none
	{ 1, 1, 2, 0 }, // 1 = text
	{ 0, 0, 2, 2 }, // 2 = affine
	{ 0, 0, 3, 0 }, // 3 = bitmap 240×160 16bpp
	{ 0, 0, 4, 0 }, // 4 = bitmap 240×160 8bpp  (2 frames)
	{ 0, 0, 5, 0 }, // 5 = bitmap 160×128 16bpp (2 frames)
};

static inline uint16_t blend_colors(uint16_t main, uint8_t aCoeff, uint16_t sub, uint8_t bCoeff) {
	uint32_t aR = main & 0x1f;
	uint32_t aG = (main >> 5) & 0x1f;
	uint32_t aB = (main >> 10) & 0x1f;

	uint32_t bR = sub & 0x1f;
	uint32_t bG = (sub >> 5) & 0x1f;
	uint32_t bB = (sub >> 10) & 0x1f;

	uint32_t r = (aR * aCoeff + bR * bCoeff) >> 4;
	uint32_t g = (aG * aCoeff + bG * bCoeff) >> 4;
	uint32_t b = (aB * aCoeff + bB * bCoeff) >> 4;

	if (r > 31) r = 31;
	if (g > 31) g = 31;
	if (b > 31) b = 31;

	return (uint16_t)(r | (g << 5) | (b << 10));
}

void ppu_init(PPU *ppu) {
	ppu_reset(ppu);
}

void ppu_reset(PPU *ppu) {
	ppu->dispcnt = 0x0080;
	ppu->dispstat = 0;
	ppu->vcount = 0;
	ppu->scanline_cycles = 0;
	ppu->hblank_started = false;
	ppu->vblank_started = false;
	ppu->irq_vblank = false;
	ppu->irq_hblank = false;
	ppu->irq_vcount = false;
	ppu->win0h = 0;
	ppu->win1h = 0;
	ppu->win0v = 0;
	ppu->win1v = 0;
	ppu->winin = 0;
	ppu->winout = 0;
	ppu->bldcnt = 0;
	ppu->bldalpha = 0;
	ppu->bldy = 0;
	for (int i = 0; i < 2; i++) {
		ppu->bg_pa[i] = 0x100;
		ppu->bg_pb[i] = 0;
		ppu->bg_pc[i] = 0;
		ppu->bg_pd[i] = 0x100;
		ppu->bg_x[i] = 0;
		ppu->bg_y[i] = 0;
		ppu->bg_ref_x[i] = 0;
		ppu->bg_ref_y[i] = 0;
	}
}

void ppu_draw_scanline(PPU *ppu) {
	int sy = ppu->vcount;
	int buf_offs = sy * GBA_WIDTH;

	struct SpritePixel {
		uint16_t color;
		uint8_t prio;
		bool semi;
		bool exists;
	};

	struct SpritePixel top_obj[GBA_WIDTH];
	memset(top_obj, 0, sizeof(top_obj));
	bool obj_win[GBA_WIDTH] = { false };

	uint16_t bg_buf[4][GBA_WIDTH];
	for (int bg = 0; bg < 4; bg++) {
		for (int x = 0; x < GBA_WIDTH; x++) {
			bg_buf[bg][x] = 0x8000;
		}
	}

	if (ppu->fblank) {
		for (int sx = 0; sx < GBA_WIDTH; sx++) {
			host_screen_buffer[buf_offs + sx] = 0xffffffff;
		}
		return;
	}

	if (ppu->en_obj) {
		int max_cycles = (ppu->hblank_oam) ? 954 : 1210;
		int used_cycles = 0;

		for (int s = 0; s < 128; s++) {
			if (used_cycles >= max_cycles) break;
			if (!ppu->sprites[s].affine && ppu->sprites[s].db_dis) continue;

			int y = ppu->sprites[s].y;
			int height = OBJ_SIZE[ppu->sprites[s].size][ppu->sprites[s].shape][1];

			int rel_y = (sy - y) & 0xff;
			if (!ppu->sprites[s].affine && rel_y >= height) continue;

			int py = ppu->sprites[s].vflip ? (height - 1 - rel_y) : rel_y;

			int x = ppu->sprites[s].x;
			int width = OBJ_SIZE[ppu->sprites[s].size][ppu->sprites[s].shape][0];

			int prio = ppu->sprites[s].prio;
			int mode = ppu->sprites[s].mode;
			bool semi = (mode == 1);
			bool is_win = (mode == 2);

			if (ppu->sprites[s].affine) {
				int bbox_w = (ppu->sprites[s].affine && ppu->sprites[s].db_dis) ? width * 2 : width;
				int bbox_h = (ppu->sprites[s].affine && ppu->sprites[s].db_dis) ? height * 2 : height;
				if (rel_y >= bbox_h) continue;

				used_cycles += width;

				int param = ppu->sprites[s].param;
				int16_t pa = ppu->affine_params[param].pa;
				int16_t pb = ppu->affine_params[param].pb;
				int16_t pc = ppu->affine_params[param].pc;
				int16_t pd = ppu->affine_params[param].pd;

				int dy = rel_y - (bbox_h / 2);

				for (int i = 0; i < bbox_w; i++) {
					int sx = (x + i) & 0x1ff;
					if (sx >= GBA_WIDTH) continue;

					int dx = i - (bbox_w / 2);
					int px = ((pa * dx + pb * dy) >> 8) + (width / 2);
					int py = ((pc * dx + pd * dy) >> 8) + (height / 2);
					if (px < 0 || px >= width || py < 0 || py >= height) continue;

					int tx = px / 8;
					int ty = py / 8;
					int tpx = px % 8;
					int tpy = py % 8;

					bool is_8bpp = ppu->sprites[s].pal_m;
					int base_tile = ppu->sprites[s].tile & (is_8bpp ? ~1 : 0x3ff);

					int tile_num = ppu->obj_vram_d ?
						(base_tile + (ty * (width / 8) + tx) * (is_8bpp ? 2 : 1))
						: (base_tile + ty * 32 + tx * (is_8bpp ? 2 : 1));

					int tile_addr = 0x10000 + ((tile_num & 0x3ff) * 32);
					int pal_idx;

					if (is_8bpp) {
						pal_idx = ppu->vram[tile_addr + tpy * 8 + tpx];
					} else {
						pal_idx = ppu->vram[tile_addr + tpy * 4 + (tpx / 2)];
						pal_idx = (tpx & 1) ? pal_idx >> 4 : pal_idx & 0xf;
					}

					bool higher_prio = !top_obj[sx].exists || prio < top_obj[sx].prio;

					if (pal_idx != 0) {
						if (is_win) {
							obj_win[sx] = true;
						} else if (higher_prio) {
							if (!is_8bpp) pal_idx = (ppu->sprites[s].pal * 16) + pal_idx;
							uint16_t color = ppu->colors[0x100 + pal_idx] & 0x7fff;
							top_obj[sx] = (struct SpritePixel){ color, (uint8_t)prio, semi, true };
						}
					} else if (higher_prio && top_obj[sx].exists) {
						top_obj[sx].prio = (uint8_t)prio;
					}
				}
			} else {
				used_cycles += width;

				for (int i = 0; i < width; i++) {
					int sx = (x + i) & 0x1ff;
					if (sx >= GBA_WIDTH) continue;

					int px = ppu->sprites[s].hflip ? (width - 1 - i) : i;
					int tx = px / 8;
					int ty = py / 8;
					int tpx = px % 8;
					int tpy = py % 8;

					bool is_8bpp = ppu->sprites[s].pal_m;
					int base_tile = ppu->sprites[s].tile & (is_8bpp ? ~1 : 0x3ff);

					int tile_num = ppu->obj_vram_d ?
						(base_tile + (ty * (width / 8) + tx) * (is_8bpp ? 2 : 1))
						: (base_tile + ty * 32 + tx * (is_8bpp ? 2 : 1));

					int tile_addr = 0x10000 + ((tile_num & 0x3ff) * 32);
					int pal_idx;

					if (is_8bpp) {
						pal_idx = ppu->vram[tile_addr + tpy * 8 + tpx];
					} else {
						pal_idx = ppu->vram[tile_addr + tpy * 4 + (tpx / 2)];
						pal_idx = (tpx & 1) ? pal_idx >> 4 : pal_idx & 0xf;
					}

					bool higher_prio = !top_obj[sx].exists || prio < top_obj[sx].prio;

					if (pal_idx != 0) {
						if (is_win) {
							obj_win[sx] = true;
						} else if (higher_prio) {
							if (!is_8bpp) pal_idx += ppu->sprites[s].pal * 16;
							uint16_t color = ppu->colors[0x100 + pal_idx] & 0x7fff;
							top_obj[sx] = (struct SpritePixel){ color, (uint8_t)prio, semi, true };
						}
					} else if (higher_prio && top_obj[sx].exists) {
						top_obj[sx].prio = (uint8_t)prio;
					}
				}
			}
		}
	}

	for (int bg = 0; bg < 4; bg++) {
		if (!(ppu->dispcnt >> (8 + bg) & 1)) continue;

		switch (BG_TYPES[ppu->bg_mode][bg]) {
			case 0: break;
			case 1: { // text
				int char_base = ppu->bg_ctrl[bg].c_base * 0x4000;
				int screen_base = ppu->bg_ctrl[bg].s_base * 0x800;

				bool is_8bpp = ppu->bg_ctrl[bg].pal_m;

				int map_w = ppu->bg_ctrl[bg].size_x ? 512 : 256;
				int map_h = ppu->bg_ctrl[bg].size_y ? 512 : 256;

				int my = (sy + ppu->bgvofs[bg]) & (map_h - 1);
				int tile_y = (my >> 3) & 31;
				int y_block = (my >= 256) ? ((map_w == 512) ? 2 : 1) : 0;
				int tpy_base = my & 7;

				for (int sx = 0; sx < GBA_WIDTH; sx++) {
					int mx = (sx + ppu->bghofs[bg]) & (map_w - 1);
					int tile_x = (mx >> 3) & 31;
					int block = y_block + (mx >= 256 ? 1 : 0);

					int se_idx = (block << 10) | (tile_y << 5) | tile_x;
					int se_addr = screen_base + (se_idx << 1);
					if (se_addr >= 0x18000) continue;

					uint16_t se = ppu->vram16[se_addr >> 1];

					int tile_num = se & 0x3ff;
					bool hflip = se & 0x0400;
					bool vflip = se & 0x0800;
					int pal_bank = (se >> 12) & 0x0f;

					int tpx = mx & 7;
					int tpy = vflip ? (7 - tpy_base) : tpy_base;
					if (hflip) tpx = 7 - tpx;

					int pal_idx;

					if (is_8bpp) {
						int tile_addr = char_base + tile_num * 64;
						if (tile_addr >= 0x18000) continue;
						pal_idx = ppu->vram[tile_addr + tpy * 8 + tpx];
					} else {
						int tile_addr = char_base + tile_num * 32;
						if (tile_addr >= 0x18000) continue;
						pal_idx = ppu->vram[tile_addr + tpy * 4 + (tpx >> 1)];
						pal_idx = (tpx & 1) ? pal_idx >> 4 : pal_idx & 0xf;
					}
					if (pal_idx == 0) continue;

					if (!is_8bpp) pal_idx += pal_bank * 16;
					bg_buf[bg][sx] = ppu->colors[pal_idx] & 0x7fff;
				}
			} break;
			case 2: { // affine
				int idx = bg - 2;
				int char_base = ppu->bg_ctrl[bg].c_base * 0x4000;
				int screen_base = ppu->bg_ctrl[bg].s_base * 0x800;

				int size = (ppu->bg_ctrl[bg].size_y << 1) | ppu->bg_ctrl[bg].size_x;
				uint32_t dim = 128 << size;
				uint32_t column_count = dim >> 3;
				bool wrap = ppu->bg_ctrl[bg].wrap;
				uint32_t wrap_mask = wrap ? (dim - 1) : 0xfffff;

				int16_t pa = ppu->bg_pa[idx];
				int16_t pc = ppu->bg_pc[idx];
				int32_t cx = ppu->bg_x[idx];
				int32_t cy = ppu->bg_y[idx];

				for (int sx = 0; sx < GBA_WIDTH; sx++) {
					uint32_t x_pos = (uint32_t)(cx >> 8) & wrap_mask;
					uint32_t y_pos = (uint32_t)(cy >> 8) & wrap_mask;

					if (x_pos < dim && y_pos < dim) {
						uint32_t map_addr = screen_base + (y_pos >> 3) * column_count + (x_pos >> 3);
						if (map_addr < 0x18000) {
							uint8_t tile_num = ppu->vram[map_addr];
							uint32_t tile_row = y_pos & 7;
							uint32_t tile_col = x_pos & 7;
							uint32_t tile_addr = (char_base + tile_num * 64 + tile_row * 8 + tile_col) & 0xffff;
							uint8_t pal_idx = ppu->vram[tile_addr];
							if (pal_idx != 0) {
								bg_buf[bg][sx] = ppu->colors[pal_idx] & 0x7fff;
							}
						}
					}

					cx += pa;
					cy += pc;
				}
			} break;
			case 3: { // bitmap 240×160 16bpp
				int idx = 0;
				int16_t pa = ppu->bg_pa[idx];
				int16_t pc = ppu->bg_pc[idx];
				int32_t cx = ppu->bg_x[idx];
				int32_t cy = ppu->bg_y[idx];
				for (int sx = 0; sx < GBA_WIDTH; sx++) {
					int32_t px = cx >> 8;
					int32_t py = cy >> 8;
					if (px >= 0 && px < 240 && py >= 0 && py < 160) {
						bg_buf[bg][sx] = ppu->vram16[py * 240 + px] & 0x7fff;
					}
					cx += pa;
					cy += pc;
				}
			} break;
			case 4: { // bitmap 240×160 8bpp  (2 frames)
				int idx = 0;
				int16_t pa = ppu->bg_pa[idx];
				int16_t pc = ppu->bg_pc[idx];
				int32_t cx = ppu->bg_x[idx];
				int32_t cy = ppu->bg_y[idx];
				int frame_base = ppu->frame_sel ? 0xa000 : 0;
				for (int sx = 0; sx < GBA_WIDTH; sx++) {
					int32_t px = cx >> 8;
					int32_t py = cy >> 8;
					if (px >= 0 && px < 240 && py >= 0 && py < 160) {
						uint8_t pal_idx = ppu->vram[frame_base + py * 240 + px];
						if (pal_idx != 0) {
							bg_buf[bg][sx] = ppu->colors[pal_idx] & 0x7fff;
						}
					}
					cx += pa;
					cy += pc;
				}
			} break;
			case 5: { // bitmap 160×128 16bpp (2 frames)
				int idx = 0;
				int16_t pa = ppu->bg_pa[idx];
				int16_t pc = ppu->bg_pc[idx];
				int32_t cx = ppu->bg_x[idx];
				int32_t cy = ppu->bg_y[idx];
				int frame_base = ppu->frame_sel ? 0xa000 : 0;
				for (int sx = 0; sx < GBA_WIDTH; sx++) {
					int32_t px = cx >> 8;
					int32_t py = cy >> 8;
					if (px >= 0 && px < 160 && py >= 0 && py < 128) {
						bg_buf[bg][sx] = ppu->vram16[(frame_base >> 1) + py * 160 + px] & 0x7fff;
					}
					cx += pa;
					cy += pc;
				}
			} break;
		}
	}

	bool win_active = (ppu->dispcnt & 0xe000) != 0;
	bool in_win0_y = false;
	bool in_win1_y = false;
	int win0_x1 = 0, win0_x2 = 0;
	int win1_x1 = 0, win1_x2 = 0;

	if (ppu->en_win0) {
		int y1 = (ppu->win0v >> 8) & 0xff;
		int y2 = ppu->win0v & 0xff;
		in_win0_y = (y1 <= y2) ? (sy >= y1 && sy < y2) : (sy >= y1 || sy < y2);
		win0_x1 = (ppu->win0h >> 8) & 0xff;
		win0_x2 = ppu->win0h & 0xff;
	}
	if (ppu->en_win1) {
		int y1 = (ppu->win1v >> 8) & 0xff;
		int y2 = ppu->win1v & 0xff;
		in_win1_y = (y1 <= y2) ? (sy >= y1 && sy < y2) : (sy >= y1 || sy < y2);
		win1_x1 = (ppu->win1h >> 8) & 0xff;
		win1_x2 = ppu->win1h & 0xff;
	}

	struct Pixel {
		uint16_t color;
		uint8_t priority;
		uint8_t layer;
		bool semi;
	};

	uint16_t bldcnt = ppu->bldcnt;
	uint8_t eva = ppu->bldalpha & 0x1f;
	uint8_t evb = (ppu->bldalpha >> 8) & 0x1f;
	uint8_t evy = ppu->bldy & 0x1f;
	if (eva > 16) eva = 16;
	if (evb > 16) evb = 16;
	if (evy > 16) evy = 16;
	int effect = (bldcnt >> 6) & 3;

	uint16_t bd_color = ppu->colors[0] & 0x7fff;

	for (int x = 0; x < GBA_WIDTH; x++) {
		uint8_t win_mask = 0x3f;
		if (win_active) {
			if (ppu->en_win0 && in_win0_y &&
				((win0_x1 <= win0_x2) ? (x >= win0_x1 && x < win0_x2) : (x >= win0_x1 || x < win0_x2))) {
				win_mask = ppu->winin & 0x3f;
			} else if (ppu->en_win1 && in_win1_y &&
				((win1_x1 <= win1_x2) ? (x >= win1_x1 && x < win1_x2) : (x >= win1_x1 || x < win1_x2))) {
				win_mask = (ppu->winin >> 8) & 0x3f;
			} else if (ppu->en_wino && obj_win[x]) {
				win_mask = (ppu->winout >> 8) & 0x3f;
			} else {
				win_mask = ppu->winout & 0x3f;
			}
		}

		struct Pixel main = { bd_color, 0xff, 5, false };
		struct Pixel sub  = { bd_color, 0xff, 5, false };

		if ((win_mask & (1 << 4)) && top_obj[x].exists) {
			main = (struct Pixel){ top_obj[x].color, top_obj[x].prio, 4, top_obj[x].semi };
		}

		for (int bg = 0; bg < 4; bg++) {
			if ((win_mask & (1 << bg)) && ((bg_buf[bg][x] & 0x8000) == 0)) {
				uint8_t bg_prio = ppu->bg_ctrl[bg].prio;
				uint16_t bg_color = bg_buf[bg][x] & 0x7fff;
				if (bg_prio < main.priority) {
					sub = main;
					main = (struct Pixel){ bg_color, bg_prio, (uint8_t)bg, false };
				} else if (bg_prio < sub.priority) {
					sub = (struct Pixel){ bg_color, bg_prio, (uint8_t)bg, false };
				}
			}
		}

		uint16_t final_color = main.color;

		if (main.layer == 4 && main.semi && ((bldcnt >> (8 + sub.layer)) & 1)) {
			final_color = blend_colors(main.color, eva, sub.color, evb);
		} else {
			if ((win_mask & 0x20) && ((bldcnt >> main.layer) & 1)) {
				if (effect == 1) { // alpha blend
					if ((bldcnt >> (8 + sub.layer)) & 1) {
						final_color = blend_colors(main.color, eva, sub.color, evb);
					}
				} else if (effect == 2) { // brightness increase
					if (evy > 0) {
						final_color = blend_colors(main.color, 16 - evy, 0x7fff, evy);
					}
				} else if (effect == 3) { // brightness decrease
					if (evy > 0) {
						final_color = blend_colors(main.color, 16 - evy, 0x0000, evy);
					}
				}
			}
		}

		host_screen_buffer[buf_offs + x] =
			0xff000000
			| (final_color & 0x001f) << 19
			| (final_color & 0x03e0) << 6
			| (final_color & 0x7c00) >> 7;
	}
}

void ppu_clock(PPU *ppu) {
	ppu->scanline_cycles++;

	if (ppu->scanline_cycles == 960 && ppu->vcount < 160) {
		ppu_draw_scanline(ppu);
		ppu->bg_x[0] += ppu->bg_pb[0];
		ppu->bg_y[0] += ppu->bg_pd[0];
		ppu->bg_x[1] += ppu->bg_pb[1];
		ppu->bg_y[1] += ppu->bg_pd[1];
		ppu->hblank_started = true;
		if (ppu->dispstat & (1 << 4)) {
			ppu->irq_hblank = true;
		}
	}

	if (ppu->scanline_cycles >= 1232) {
		ppu->scanline_cycles = 0;
		ppu->vcount++;

		if (ppu->vcount == 160) {
			ppu->vblank_started = true;
			if (ppu->dispstat & (1 << 3)) {
				ppu->irq_vblank = true;
			}
		} else if (ppu->vcount >= 228) {
			ppu->vcount = 0;
			ppu->bg_x[0] = ppu->bg_ref_x[0];
			ppu->bg_y[0] = ppu->bg_ref_y[0];
			ppu->bg_x[1] = ppu->bg_ref_x[1];
			ppu->bg_y[1] = ppu->bg_ref_y[1];
			host_copy_buffer();
		}

		if ((ppu->dispstat & (1 << 5)) && ppu->vcount == (ppu->dispstat >> 8)) {
			ppu->irq_vcount = true;
		}
	}
}

static inline int32_t sign_ext28(uint32_t val) {
	return (int32_t)(val << 4) >> 4;
}

uint8_t ppu_read8(PPU *ppu, uint8_t addr) {
	switch (addr) {
		case 0x00: return ppu->dispcnt & 0xff;
		case 0x01: return (ppu->dispcnt >> 8) & 0xff;

		case 0x04: {
			uint8_t stat = ppu->dispstat & 0xf8;
			if (ppu->vcount >= 160 && ppu->vcount < 227) stat |= (1 << 0);
			if (ppu->scanline_cycles >= 960) stat |= (1 << 1);
			if (ppu->vcount == (ppu->dispstat >> 8)) stat |= (1 << 2);
			return stat;
		}
		case 0x05: return (ppu->dispstat >> 8) & 0xff;

		case 0x06: return ppu->vcount & 0xff;
		case 0x07: return (ppu->vcount >> 8) & 0xff;

		case 0x08 ... 0x0f: {
			int bg = (addr - 0x08) >> 1;
			return (addr & 1) ? (ppu->bgcnt[bg] >> 8) : (ppu->bgcnt[bg] & 0xff);
		}

		case 0x48: return ppu->winin & 0xff;
		case 0x49: return (ppu->winin >> 8) & 0xff;
		case 0x4a: return ppu->winout & 0xff;
		case 0x4b: return (ppu->winout >> 8) & 0xff;
		case 0x50: return ppu->bldcnt & 0xff;
		case 0x51: return (ppu->bldcnt >> 8) & 0xff;
		case 0x52: return ppu->bldalpha & 0xff;
		case 0x53: return (ppu->bldalpha >> 8) & 0xff;

		default:
			return 0;
	}
}

uint16_t ppu_read16(PPU *ppu, uint8_t addr) {
	switch (addr) {
		case 0x00: return ppu->dispcnt;
		case 0x04: {
			uint16_t stat = ppu->dispstat & 0xfff8;
			if (ppu->vcount >= 160 && ppu->vcount < 227) stat |= (1 << 0);
			if (ppu->scanline_cycles >= 960) stat |= (1 << 1);
			if (ppu->vcount == (ppu->dispstat >> 8)) stat |= (1 << 2);
			return stat;
		}
		case 0x06: return ppu->vcount;
		case 0x08: case 0x0a: case 0x0c: case 0x0e:
			return ppu->bgcnt[(addr - 0x08) >> 1];
		case 0x48: return ppu->winin;
		case 0x4a: return ppu->winout;
		case 0x50: return ppu->bldcnt;
		case 0x52: return ppu->bldalpha;
		default:
			return (uint16_t)ppu_read8(ppu, addr) | ((uint16_t)ppu_read8(ppu, addr + 1) << 8);
	}
}

uint32_t ppu_read32(PPU *ppu, uint8_t addr) {
	return (uint32_t)ppu_read16(ppu, addr) | ((uint32_t)ppu_read16(ppu, addr + 2) << 16);
}

void ppu_write8(PPU *ppu, uint8_t addr, uint8_t val) {
	switch (addr) {
		case 0x00: ppu->dispcnt = (ppu->dispcnt & 0xff00) | val; break;
		case 0x01: ppu->dispcnt = (ppu->dispcnt & 0x00ff) | ((uint16_t)val << 8); break;
		case 0x04:
			ppu->dispstat = (ppu->dispstat & 0xff00) | (val & 0xf8);
			break;
		case 0x05: {
			uint8_t old_lyc = ppu->dispstat >> 8;
			ppu->dispstat = (ppu->dispstat & 0x00ff) | ((uint16_t)val << 8);
			if (old_lyc != val && (ppu->dispstat & (1 << 5)) && ppu->vcount == val) {
				ppu->irq_vcount = true;
			}
			break;
		}

		case 0x08 ... 0x0f: {
			int bg = (addr - 0x08) >> 1;
			if (addr & 1) ppu->bgcnt[bg] = (ppu->bgcnt[bg] & 0x00ff) | ((uint16_t)val << 8);
			else          ppu->bgcnt[bg] = (ppu->bgcnt[bg] & 0xff00) | val;
			break;
		}

		case 0x10 ... 0x1f: {
			int bg = (addr - 0x10) >> 2;
			uint16_t *ofs = (addr & 2) ? &ppu->bgvofs[bg] : &ppu->bghofs[bg];
			if (addr & 1) *ofs = (*ofs & 0x00ff) | ((uint16_t)(val & 1) << 8);
			else          *ofs = (*ofs & 0xff00) | val;
			break;
		}

		case 0x20 ... 0x27:
		case 0x30 ... 0x37: {
			int idx = (addr >= 0x30) ? 1 : 0;
			int16_t *p = NULL;
			switch (addr & 0x06) {
				case 0x00: p = &ppu->bg_pa[idx]; break;
				case 0x02: p = &ppu->bg_pb[idx]; break;
				case 0x04: p = &ppu->bg_pc[idx]; break;
				case 0x06: p = &ppu->bg_pd[idx]; break;
			}
			uint16_t cur = (uint16_t)*p;
			if (addr & 1) *p = (int16_t)((cur & 0x00ff) | ((uint16_t)val << 8));
			else          *p = (int16_t)((cur & 0xff00) | val);
			break;
		}

		case 0x28 ... 0x2f:
		case 0x38 ... 0x3f: {
			int idx = (addr >= 0x30) ? 1 : 0;
			bool is_y = (addr & 0x04) != 0;
			int shift = (addr & 3) * 8;
			uint32_t mask = ~(0xffu << shift);
			if (is_y) {
				uint32_t cur = ((uint32_t)ppu->bg_ref_y[idx] & mask) | ((uint32_t)val << shift);
				ppu->bg_ref_y[idx] = sign_ext28(cur);
				ppu->bg_y[idx] = ppu->bg_ref_y[idx];
			} else {
				uint32_t cur = ((uint32_t)ppu->bg_ref_x[idx] & mask) | ((uint32_t)val << shift);
				ppu->bg_ref_x[idx] = sign_ext28(cur);
				ppu->bg_x[idx] = ppu->bg_ref_x[idx];
			}
			break;
		}

		case 0x40: ppu->win0h = (ppu->win0h & 0xff00) | val; break;
		case 0x41: ppu->win0h = (ppu->win0h & 0x00ff) | ((uint16_t)val << 8); break;
		case 0x42: ppu->win1h = (ppu->win1h & 0xff00) | val; break;
		case 0x43: ppu->win1h = (ppu->win1h & 0x00ff) | ((uint16_t)val << 8); break;
		case 0x44: ppu->win0v = (ppu->win0v & 0xff00) | val; break;
		case 0x45: ppu->win0v = (ppu->win0v & 0x00ff) | ((uint16_t)val << 8); break;
		case 0x46: ppu->win1v = (ppu->win1v & 0xff00) | val; break;
		case 0x47: ppu->win1v = (ppu->win1v & 0x00ff) | ((uint16_t)val << 8); break;
		case 0x48: ppu->winin = (ppu->winin & 0xff00) | val; break;
		case 0x49: ppu->winin = (ppu->winin & 0x00ff) | ((uint16_t)val << 8); break;
		case 0x4a: ppu->winout = (ppu->winout & 0xff00) | val; break;
		case 0x4b: ppu->winout = (ppu->winout & 0x00ff) | ((uint16_t)val << 8); break;

		case 0x50: ppu->bldcnt = (ppu->bldcnt & 0xff00) | val; break;
		case 0x51: ppu->bldcnt = (ppu->bldcnt & 0x00ff) | ((uint16_t)val << 8); break;
		case 0x52: ppu->bldalpha = (ppu->bldalpha & 0xff00) | val; break;
		case 0x53: ppu->bldalpha = (ppu->bldalpha & 0x00ff) | ((uint16_t)val << 8); break;
		case 0x54: ppu->bldy = (ppu->bldy & 0xff00) | val; break;
		case 0x55: ppu->bldy = (ppu->bldy & 0x00ff) | ((uint16_t)val << 8); break;

		default: break;
	}
}

void ppu_write16(PPU *ppu, uint8_t addr, uint16_t val) {
	switch (addr) {
		case 0x00: ppu->dispcnt = val; break;

		case 0x08: case 0x0a: case 0x0c: case 0x0e:
			ppu->bgcnt[(addr - 0x08) >> 1] = val;
			break;

		case 0x10 ... 0x1e: {
			int bg = (addr - 0x10) >> 2;
			if (addr & 2) ppu->bgvofs[bg] = val & 0x01ff;
			else          ppu->bghofs[bg] = val & 0x01ff;
			break;
		}

		case 0x20: ppu->bg_pa[0] = (int16_t)val; break;
		case 0x22: ppu->bg_pb[0] = (int16_t)val; break;
		case 0x24: ppu->bg_pc[0] = (int16_t)val; break;
		case 0x26: ppu->bg_pd[0] = (int16_t)val; break;
		case 0x30: ppu->bg_pa[1] = (int16_t)val; break;
		case 0x32: ppu->bg_pb[1] = (int16_t)val; break;
		case 0x34: ppu->bg_pc[1] = (int16_t)val; break;
		case 0x36: ppu->bg_pd[1] = (int16_t)val; break;

		case 0x28: case 0x2a: case 0x2c: case 0x2e:
		case 0x38: case 0x3a: case 0x3c: case 0x3e: {
			int idx = (addr >= 0x30) ? 1 : 0;
			bool is_y = (addr & 0x04) != 0;
			int shift = (addr & 2) ? 16 : 0;
			uint32_t mask = (shift == 16) ? 0x0000ffff : 0xffff0000;
			if (is_y) {
				uint32_t cur = ((uint32_t)ppu->bg_ref_y[idx] & mask) | ((uint32_t)val << shift);
				ppu->bg_ref_y[idx] = sign_ext28(cur);
				ppu->bg_y[idx] = ppu->bg_ref_y[idx];
			} else {
				uint32_t cur = ((uint32_t)ppu->bg_ref_x[idx] & mask) | ((uint32_t)val << shift);
				ppu->bg_ref_x[idx] = sign_ext28(cur);
				ppu->bg_x[idx] = ppu->bg_ref_x[idx];
			}
			break;
		}

		case 0x40: ppu->win0h = val; break;
		case 0x42: ppu->win1h = val; break;
		case 0x44: ppu->win0v = val; break;
		case 0x46: ppu->win1v = val; break;
		case 0x48: ppu->winin = val; break;
		case 0x4a: ppu->winout = val; break;

		case 0x50: ppu->bldcnt = val; break;
		case 0x52: ppu->bldalpha = val; break;
		case 0x54: ppu->bldy = val; break;

		default:
			ppu_write8(ppu, addr, val);
			ppu_write8(ppu, addr + 1, (val >> 8));
			break;
	}
}

void ppu_write32(PPU *ppu, uint8_t addr, uint32_t val) {
	switch (addr) {
		case 0x28: ppu->bg_x[0] = ppu->bg_ref_x[0] = sign_ext28(val); break;
		case 0x2c: ppu->bg_y[0] = ppu->bg_ref_y[0] = sign_ext28(val); break;
		case 0x38: ppu->bg_x[1] = ppu->bg_ref_x[1] = sign_ext28(val); break;
		case 0x3c: ppu->bg_y[1] = ppu->bg_ref_y[1] = sign_ext28(val); break;
		default:
			ppu_write16(ppu, addr, val);
			ppu_write16(ppu, addr + 2, (val >> 16));
			break;
	}
}
