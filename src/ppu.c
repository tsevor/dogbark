#include "ppu.h"
#include "host.h"

static const uint8_t OBJ_SIZE[4][3][2] = {
	// square      horizontal  vertical
	{ {  8,  8 }, { 16,  8 }, {  8, 16 } }, // 0
	{ { 16, 16 }, { 32,  8 }, {  8, 32 } }, // 1
	{ { 32, 32 }, { 32, 16 }, { 16, 32 } }, // 2
	{ { 64, 64 }, { 64, 32 }, { 32, 64 } }, // 3
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
			}

		}
	}

	// do bgs here

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
		default: break;
	}
}

void ppu_write16(PPU *ppu, uint8_t addr, uint16_t val) {
	switch (addr) {
		case 0x00: ppu->dispcnt = val; break;
		case 0x04: ppu->dispstat = (val & 0xfff8); break;
		default:
			ppu_write8(ppu, addr, val);
			ppu_write8(ppu, addr + 1, (val >> 8));
			break;
	}
}

void ppu_write32(PPU *ppu, uint8_t addr, uint32_t val) {
	ppu_write16(ppu, addr, val);
	ppu_write16(ppu, addr + 2, (val >> 16));
}
