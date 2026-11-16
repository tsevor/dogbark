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
	if (bus->cycles++ == 16000000) {
		bus->cycles = 0;
		bus->vblank_started = true; // force for now
	}
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

uint8_t bus_read8(Bus *bus, uint32_t addr) {
	uint8_t region = addr >> 24;
	switch (region) {
		case 0x00:
			if (addr < 0x4000) return bus->bios[addr];
			return 0;
		case 0x02:
			return bus->ewram[addr & 0x3ffff];
		case 0x03:
			return bus->iwram[addr & 0x7fff];
		case 0x08 ... 0x0d: {
			uint32_t rom_addr = addr & 0x01ffffff;
			if (rom_addr < bus->cart->rom_size) return bus->cart->rom[rom_addr];
			return 0;
		}
		case 0x0e: case 0x0f:
			return cart_read(bus->cart, addr);
		default:
			return 0;
	}
}

uint16_t bus_read16(Bus *bus, uint32_t addr) {
	uint32_t aligned = addr & ~1;
	uint8_t region = addr >> 24;
	switch (region) {
		case 0x00:
			if (aligned < 0x4000) return *(uint16_t*)(bus->bios + aligned);
			return 0;
		case 0x02:
			return *(uint16_t*)(bus->ewram + (aligned & 0x3ffff));
		case 0x03:
			return *(uint16_t*)(bus->iwram + (aligned & 0x7fff));
		case 0x08 ... 0x0d: {
			uint32_t rom_addr = aligned & 0x01ffffff;
			if (rom_addr + 1 < bus->cart->rom_size) return *(uint16_t*)(bus->cart->rom + rom_addr);
			return 0;
		}
		default:
			return 0;
	}
}

uint32_t bus_read32(Bus *bus, uint32_t addr) {
	uint32_t aligned = addr & ~3;
	uint32_t val = 0;
	uint8_t region = addr >> 24;
	switch (region) {
		case 0x00:
			if (aligned < 0x4000) val = *(uint32_t*)(bus->bios + aligned);
			break;
		case 0x02:
			val = *(uint32_t*)(bus->ewram + (aligned & 0x3ffff));
			break;
		case 0x03:
			val = *(uint32_t*)(bus->iwram + (aligned & 0x7fff));
			break;
		case 0x08 ... 0x0d: {
			uint32_t rom_addr = aligned & 0x01ffffff;
			if (rom_addr + 3 < bus->cart->rom_size) val = *(uint32_t*)(bus->cart->rom + rom_addr);
			break;
		}
		default:
			break;
	}

	uint32_t roll = (addr & 3) << 3;
	if (roll) val = (val >> roll) | (val << (32 - roll));
	return val;
}

void bus_write8(Bus *bus, uint32_t addr, uint8_t val) {
	uint8_t region = addr >> 24;
	switch (region) {
		case 0x02:
			bus->ewram[addr & 0x3ffff] = val;
			break;
		case 0x03:
			bus->iwram[addr & 0x7fff] = val;
			break;
		case 0x0e: case 0x0f:
			cart_write(bus->cart, addr, val);
			break;
		default:
			ldeb1("bus_write8: unmapped: 0x%08x: %02x\n", addr, val);
			break;
	}
}

void bus_write16(Bus *bus, uint32_t addr, uint16_t val) {
	uint32_t aligned = addr & ~1;
	uint8_t region = addr >> 24;
	switch (region) {
		case 0x02:
			*(uint16_t*)(bus->ewram + (aligned & 0x3ffff)) = val;
			break;
		case 0x03:
			*(uint16_t*)(bus->iwram + (aligned & 0x7fff)) = val;
			break;
		default:
			ldeb1("bus_write16: unmapped: 0x%08x: %04x\n", addr, val);
			break;
	}
}

void bus_write32(Bus *bus, uint32_t addr, uint32_t val) {
	uint32_t aligned = addr & ~3;
	uint8_t region = addr >> 24;
	switch (region) {
		case 0x02:
			*(uint32_t*)(bus->ewram + (aligned & 0x3ffff)) = val;
			break;
		case 0x03:
			*(uint32_t*)(bus->iwram + (aligned & 0x7fff)) = val;
			break;
		default:
			ldeb1("bus_write32: unmapped: 0x%08x: %08x\n", addr, val);
			break;
	}
}
