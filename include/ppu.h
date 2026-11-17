#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct PPU {
	uint8_t pram[0x400];
	uint8_t vram[0x18000];
	uint8_t oam[0x400];

	uint16_t dispcnt;
	uint16_t dispstat;
	uint16_t vcount;

	uint32_t scanline_cycles;
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
