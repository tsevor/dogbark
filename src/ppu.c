#include "ppu.h"
#include "host.h"

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

void ppu_init(PPU *ppu) {
	ppu_reset(ppu);
}

void ppu_reset(PPU *ppu) {
	ppu->dispcnt = 0x0080;
	ppu->dispstat = 0;
	ppu->vcount = 0;
	ppu->scanline_cycles = 0;
	ppu->vblank_started = false;
	ppu->irq_vblank = false;
}

void ppu_draw_scanline(PPU *ppu) {
	int sy = ppu->vcount;
	int buf_offs = sy * GBA_WIDTH;

	uint16_t lay[GBA_WIDTH][4];
	for (int x = 0; x < GBA_WIDTH; x++) {
		lay[x][0] = 0x8000;
		lay[x][1] = 0x8000;
		lay[x][2] = 0x8000;
		lay[x][3] = 0x8000;
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
			int width =  OBJ_SIZE[ppu->sprites[s].size][ppu->sprites[s].shape][0];

			int prio = ppu->sprites[s].prio;

			if (ppu->sprites[s].affine) {
				int bbox_w = (ppu->sprites[s].affine && ppu->sprites[s].db_dis) ? width * 2 : width;
				int bbox_h = (ppu->sprites[s].affine && ppu->sprites[s].db_dis) ? height * 2 : height;
				if (rel_y >= bbox_h) continue;

				used_cycles += width;

				int param = ppu->sprites[s].param;
				int pa = ppu->affine_params[param].pa;
				int pb = ppu->affine_params[param].pb;
				int pc = ppu->affine_params[param].pc;
				int pd = ppu->affine_params[param].pd;

				int dy = rel_y - (bbox_h / 2);

				for (int i = 0; i < bbox_w; i++) {
					int sx = (x + i) & 0x1ff;
					if (sx >= GBA_WIDTH || (lay[sx][prio] & 0x8000) == 0) continue;

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
						if (tpx & 1) {
							pal_idx = pal_idx >> 4;
						} else {
							pal_idx = pal_idx & 0xf;
						}
					}

					if (pal_idx == 0) continue;

					if (!is_8bpp) pal_idx = (ppu->sprites[s].pal * 16) + pal_idx;
					lay[sx][prio] = ppu->colors[0x100 + pal_idx] & 0x7fff;
				}
			} else {
				used_cycles += width;

				for (int i = 0; i < width; i++) {
					int sx = (x + i) & 0x1ff;
					if (sx >= GBA_WIDTH || (lay[sx][prio] & 0x8000) == 0) continue;

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

					if (pal_idx == 0) continue;

					if (!is_8bpp) pal_idx += ppu->sprites[s].pal * 16;
					lay[sx][prio] = ppu->colors[0x100 + pal_idx] & 0x7fff;
				}
			}

		}
	}

	for (int bg = 0; bg < 4; bg++) {
		if (!(ppu->dispcnt >> (8 + bg) & 1)) continue;
		int prio = ppu->bg_ctrl[bg].prio;

		switch (BG_TYPES[ppu->bg_mode][bg]) {
			case 0: break;
			case 1: { // text
				int char_base = ppu->bg_ctrl[bg].c_base * 0x4000;
				int screen_base = ppu->bg_ctrl[bg].s_base * 0x800;

				bool is_8bpp = ppu->bg_ctrl[bg].pal_m;

				int map_w = ppu->bg_ctrl[bg].size_x ? 512 : 256;
				int map_h = ppu->bg_ctrl[bg].size_y ? 512 : 256;

				for (int sx = 0; sx < GBA_WIDTH; sx++) {
					if ((lay[sx][prio] & 0x8000) == 0) continue;

					int mx = (sx + ppu->bghofs[bg]) & (map_w - 1);
					int my = (sy + ppu->bgvofs[bg]) & (map_h - 1);

					int screen_addr = screen_base;
					if (mx >= 256) screen_addr += 0x800;
					if (my >= 256) screen_addr += (map_w == 512) ? 0x1000 : 0x800;

					int tx = (mx & 0xff) / 8;
					int ty = (my & 0xff) / 8;

					uint16_t entry = ppu->vram16[(screen_addr >> 1) + (ty * 32 + tx)];

					int tile_num = entry & 0x3ff;
					bool hflip = (entry >> 10) & 1;
					bool vflip = (entry >> 11) & 1;
					int pal_bank = (entry >> 12) & 0xf;

					int tpx = hflip ? 7 - (mx % 8) : mx % 8;
					int tpy = vflip ? 7 - (my % 8) : my % 8;

					int pal_idx;

					if (is_8bpp) {
						int tile_addr = char_base + tile_num * 64;
						if (tile_addr >= 0x10000) continue;
						pal_idx = ppu->vram[tile_addr + tpy * 8 + tpx];
					} else {
						int tile_addr = char_base + tile_num * 32;
						pal_idx = ppu->vram[tile_addr + tpy * 4 + (tpx / 2)];
						pal_idx = (tpx & 1) ? pal_idx >> 4 : pal_idx & 0xf;
					}
					if (pal_idx == 0) continue;

					if (!is_8bpp) pal_idx += pal_bank * 16;
					lay[sx][prio] = ppu->colors[pal_idx] & 0x7fff;
				}
			} break;
			case 2: { // affine

			} break;
			case 3: { // bitmap 240×160 16bpp

			} break;
			case 4: { // bitmap 240×160 8bpp  (2 frames)

			} break;
			case 5: { // bitmap 160×128 16bpp (2 frames)

			} break;
		}
	}

	for (int x = 0; x < GBA_WIDTH; x++) {
		uint16_t color = ppu->colors[0];
		for (int l = 0; l < 4; l++) {
			if ((lay[x][l] & 0x8000) == 0) {
				color = lay[x][l];
				break;
			}
		}
		host_screen_buffer[buf_offs + x] =
			0xff000000
			| (color & 0x001f) << 19
			| (color & 0x03e0) << 6
			| (color & 0x7c00) >> 7;
	}
}

void ppu_clock(PPU *ppu) {
	if (ppu->scanline_cycles == 0 && ppu->vcount < 160) {
		ppu_draw_scanline(ppu);
	}

	ppu->scanline_cycles++;

	if (ppu->scanline_cycles == 960 && ppu->vcount < 160) {
		ppu->hblank_started = true;
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
			host_copy_buffer();
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
		case 0x04: ppu->dispstat = (ppu->dispstat & 0xff00) | (val & 0xf8); break;
		case 0x05: ppu->dispstat = (ppu->dispstat & 0x00ff) | ((uint16_t)val << 8); break;

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
				uint32_t cur = ((uint32_t)ppu->bg_y[idx] & mask) | ((uint32_t)val << shift);
				ppu->bg_y[idx] = sign_ext28(cur);
				ppu->bg_ref_y[idx] = ppu->bg_y[idx];
			} else {
				uint32_t cur = ((uint32_t)ppu->bg_x[idx] & mask) | ((uint32_t)val << shift);
				ppu->bg_x[idx] = sign_ext28(cur);
				ppu->bg_ref_x[idx] = ppu->bg_x[idx];
			}
			break;
		}

		default: break;
	}
}

void ppu_write16(PPU *ppu, uint8_t addr, uint16_t val) {
	switch (addr) {
		case 0x00: ppu->dispcnt = val; break;
		case 0x04: ppu->dispstat = (val & 0xfff8); break;

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
