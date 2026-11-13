#include "bus.h"
#include "cart.h"
#include "log.h"

#include <string.h>

void bus_init(Bus *bus) {
	memset(bus->iwram, 0, sizeof(bus->iwram));
	memset(bus->ewram, 0, sizeof(bus->ewram));
	cpu_init(bus->cpu);
}

void bus_free(Bus *bus) {
	cart_free(bus->cart);
}

void bus_clock(Bus *bus) {
	bus->vblank_started = true; // force for now
}

void bus_reset(Bus *bus) {
	cpu_reset(bus->cpu);
}

// General Internal Memory

//   00000000-00003FFF   BIOS - System ROM         (16 KBytes)
//   00004000-01FFFFFF   Not used
//   02000000-0203FFFF   WRAM - On-board Work RAM  (256 KBytes) 2 Wait
//   02040000-02FFFFFF   Not used
//   03000000-03007FFF   WRAM - On-chip Work RAM   (32 KBytes)
//   03008000-03FFFFFF   Not used
//   04000000-040003FE   I/O Registers
//   04000400-04FFFFFF   Not used

// Internal Display Memory

//   05000000-050003FF   BG/OBJ Palette RAM        (1 Kbyte)
//   05000400-05FFFFFF   Not used
//   06000000-06017FFF   VRAM - Video RAM          (96 KBytes)
//   06018000-06FFFFFF   Not used
//   07000000-070003FF   OAM - OBJ Attributes      (1 Kbyte)
//   07000400-07FFFFFF   Not used

// External Memory (Game Pak)

//   08000000-09FFFFFF   Game Pak ROM/FlashROM (max 32MB) - Wait State 0
//   0A000000-0BFFFFFF   Game Pak ROM/FlashROM (max 32MB) - Wait State 1
//   0C000000-0DFFFFFF   Game Pak ROM/FlashROM (max 32MB) - Wait State 2
//   0E000000-0E00FFFF   Game Pak SRAM    (max 64 KBytes) - 8bit Bus width
//   0E010000-0FFFFFFF   Not used

// Unused Memory Area

//   10000000-FFFFFFFF   Not used (upper 4bits of address bus unused)

uint32_t bus_read(Bus *bus, uint32_t addr) {
	switch (addr) {
		case 0x00000000 ... 0x00003fff:
			return *(uint32_t*)(bus->bios + addr);
		case 0x08000000 ... 0x0fffffff:
			return cart_read(bus->cart, addr);
		default:
			ldeb1("bus_read: unmapped: 0x%08x\n", addr);
			return 0;
	}
}

void bus_write(Bus *bus, uint32_t addr, uint32_t val) {
	switch (addr) {
		case 0x08000000 ... 0x0fffffff:
			cart_write(bus->cart, addr, val);
			break;
		default:
			ldeb1("bus_write: unmapped: 0x%08x\n", addr);
			break;
	}
}
