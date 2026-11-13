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

	Cart *cart;
	CPU *cpu;
} Bus;

void bus_init(Bus *bus);
void bus_free(Bus *bus);

void bus_reset(Bus *bus);

void bus_clock(Bus *bus);

uint32_t bus_read(Bus *bus, uint32_t addr);
void bus_write(Bus *bus, uint32_t addr, uint32_t val);

#include "cpu.h"
