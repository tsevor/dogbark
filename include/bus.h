#pragma once

#include <stdint.h>
#include <stddef.h>

#include "apu.h"
#include "cart.h"
#include "dma.h"
#include "ppu.h"
#include "timer.h"

typedef struct CPU CPU;

typedef struct Bus {
	uint8_t *bios;
	uint8_t ewram[0x40000];
	uint8_t iwram[0x8000];

	bool vblank_started;

	uint64_t cycles;

	uint16_t io_ie;
	uint16_t io_if;
	uint8_t io_ime;
	bool halted;
	uint16_t keycnt;

	APU *apu;
	Cart *cart;
	CPU *cpu;
	DMA *dma;
	PPU *ppu;
	Timers *timers;
} Bus;

void bus_init(Bus *bus);
void bus_free(Bus *bus);

void bus_reset(Bus *bus);

void bus_clock(Bus *bus);

uint8_t bus_read8(Bus *bus, uint32_t addr);
uint16_t bus_read16(Bus *bus, uint32_t addr);
uint32_t bus_read32(Bus *bus, uint32_t addr);

void bus_write8(Bus *bus, uint32_t addr, uint8_t val);
void bus_write16(Bus *bus, uint32_t addr, uint16_t val);
void bus_write32(Bus *bus, uint32_t addr, uint32_t val);


#include "cpu.h"
