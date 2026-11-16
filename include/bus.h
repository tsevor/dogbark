#pragma once

#include <stdint.h>
#include <stddef.h>

#include "cart.h"

typedef struct CPU CPU;

typedef struct Bus {
	uint8_t *bios;
	uint8_t ewram[0x40000];
	uint8_t iwram[0x8000];

	bool vblank_started;

	uint64_t cycles;

	Cart *cart;
	CPU *cpu;
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
