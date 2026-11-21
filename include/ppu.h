#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct PPU {
	union {
		uint8_t pram[0x400];
		uint16_t colors[0x200];
	};
	union {
		uint8_t vram[0x18000];
		uint16_t vram16[0xc000];
	};
	union {
		uint8_t oam[0x400];
		struct {
			// attr 0
			uint16_t y      : 8;
			uint16_t affine : 1;
			uint16_t db_dis : 1;
			uint16_t mode   : 2;
			uint16_t mosaic : 1;
			uint16_t pal_m  : 1;
			uint16_t shape  : 2;
			// attr 1
			union {
				struct {
					uint16_t x     : 9;
					uint16_t param : 5;
					uint16_t size  : 2;
				};
				struct {
					uint16_t       : 12; // x : 9 + unused : 3
					uint16_t hflip : 1;
					uint16_t vflip : 1;
					uint16_t       : 2;  // size
				};
			};
			// attr 2
			uint16_t tile : 10;
			uint16_t prio : 2;
			uint16_t pal  : 4;
			// affine param
			uint16_t : 16;
		} sprites[128];
		struct {
			uint16_t : 16;
			uint16_t : 16;
			uint16_t : 16;
			int16_t pa;
			uint16_t : 16;
			uint16_t : 16;
			uint16_t : 16;
			int16_t pb;
			uint16_t : 16;
			uint16_t : 16;
			uint16_t : 16;
			int16_t pc;
			uint16_t : 16;
			uint16_t : 16;
			uint16_t : 16;
			int16_t pd;
		} affine_params[32];
	};

	union {
		uint16_t dispcnt;
		struct {
			uint16_t bg_mode    : 3;
			uint16_t : 1; // reserved
			uint16_t frame_sel  : 1;
			uint16_t hblank_oam : 1;
			uint16_t obj_vram_d : 1;
			uint16_t fblank     : 1;
			uint16_t en_bg0     : 1;
			uint16_t en_bg1     : 1;
			uint16_t en_bg2     : 1;
			uint16_t en_bg3     : 1;
			uint16_t en_obj     : 1;
			uint16_t en_win0    : 1;
			uint16_t en_win1    : 1;
			uint16_t en_wino    : 1;
		};
	};
	uint16_t dispstat;
	union {
		uint16_t bgcnt[4];
		struct {
			uint16_t prio   : 2;
			uint16_t c_base : 2;
			uint16_t : 2;
			uint16_t mosaic : 1;
			uint16_t pal_m  : 1;
			uint16_t s_base : 5;
			uint16_t wrap   : 1;
			uint16_t size_x : 1;
			uint16_t size_y : 1;
		} bg_ctrl[4];
	};
	uint16_t bghofs[4];
	uint16_t bgvofs[4];

	int16_t bg_pa[2];
	int16_t bg_pb[2];
	int16_t bg_pc[2];
	int16_t bg_pd[2];
	int32_t bg_x[2];
	int32_t bg_y[2];
	int32_t bg_ref_x[2];
	int32_t bg_ref_y[2];

	uint16_t vcount;

	uint32_t scanline_cycles;
	bool hblank_started;
	bool vblank_started;
	bool irq_vblank;
} PPU;

void ppu_init(PPU *ppu);
void ppu_reset(PPU *ppu);
void ppu_clock(PPU *ppu);

uint8_t ppu_read8(PPU *ppu, uint8_t addr);
uint16_t ppu_read16(PPU *ppu, uint8_t addr);
uint32_t ppu_read32(PPU *ppu, uint8_t addr);

void ppu_write8(PPU *ppu, uint8_t addr, uint8_t val);
void ppu_write16(PPU *ppu, uint8_t addr, uint16_t val);
void ppu_write32(PPU *ppu, uint8_t addr, uint32_t val);
