#include "bus.h"
#include "apu.h"
#include "cart.h"
#include "dma.h"
#include "host.h"
#include "log.h"
#include "ppu.h"
#include "timer.h"

#include <string.h>

void bus_init(Bus *bus) {
	memset(bus->iwram, 0, sizeof(bus->iwram));
	memset(bus->ewram, 0, sizeof(bus->ewram));
	memset(bus->ppu->pram, 0, sizeof(bus->ppu->pram));
	memset(bus->ppu->vram, 0, sizeof(bus->ppu->vram));
	memset(bus->ppu->oam, 0, sizeof(bus->ppu->oam));
	bus->io_ie = 0;
	bus->io_if = 0;
	bus->io_ime = 0;
	bus->halted = false;
	bus->keycnt = 0;
	apu_init(bus->apu);
	ppu_init(bus->ppu);
	dma_init(bus->dma, bus);
	timer_init(bus->timers, bus);
	cpu_init(bus->cpu);
}

void bus_free(Bus *bus) {
	cart_free(bus->cart);
}

void bus_clock(Bus *bus) {
	bus->cycles++;
	apu_clock(bus->apu);
	ppu_clock(bus->ppu);
	timer_clock(bus->timers);
	dma_clock(bus->dma);
	if (bus->ppu->hblank_started) {
		bus->ppu->hblank_started = false;
		dma_check_hblank(bus->dma);
	}
	if (bus->ppu->vblank_started) {
		bus->ppu->vblank_started = false;
		bus->vblank_started = true;
		dma_check_vblank(bus->dma);
	}
	if (bus->ppu->irq_vblank) {
		bus->ppu->irq_vblank = false;
		bus->io_if |= (1 << 0);
	}

	if (bus->keycnt & (1 << 14)) {
		uint16_t mask = bus->keycnt & 0x03ff;
		uint16_t pressed = (~host_get_controller()) & 0x03ff;
		bool cond = (bus->keycnt & (1 << 15))
			? (mask != 0 && (pressed & mask) == mask)
			: ((pressed & mask) != 0);
		if (cond) {
			bus->io_if |= (1 << 12);
		}
	}
}

void bus_reset(Bus *bus) {
	bus->io_ie = 0;
	bus->io_if = 0;
	bus->io_ime = 0;
	bus->halted = false;
	bus->keycnt = 0;
	apu_reset(bus->apu);
	ppu_reset(bus->ppu);
	dma_reset(bus->dma);
	timer_reset(bus->timers);
	cpu_reset(bus->cpu);
}

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
		case 0x04:
			switch (addr & 0xffffff) {
				case 0x000000 ... 0x00005f:
					return ppu_read8(bus->ppu, addr);
				case 0x000060 ... 0x0000af:
					return apu_read8(bus->apu, addr);
				case 0x0000b0 ... 0x0000df:
					return dma_read8(bus->dma, addr);
				case 0x000100 ... 0x00010f:
					return timer_read8(bus->timers, addr);
				case 0x000130: return host_get_controller() & 0xff;
				case 0x000131: return (host_get_controller() >> 8) & 0xff;
				case 0x000132: return bus->keycnt & 0xff;
				case 0x000133: return (bus->keycnt >> 8) & 0xff;
				case 0x000200: return bus->io_ie & 0xff;
				case 0x000201: return (bus->io_ie >> 8) & 0xff;
				case 0x000202: return bus->io_if & 0xff;
				case 0x000203: return (bus->io_if >> 8) & 0xff;
				case 0x000208: return bus->io_ime;
				default:
					return 0;
			}
		case 0x05:
			return bus->ppu->pram[addr & 0x3ff];
		case 0x06: {
			uint32_t off = addr & 0x1ffff;
			if (off >= 0x18000) off -= 0x8000;
			return bus->ppu->vram[off];
		}
		case 0x07:
			return bus->ppu->oam[addr & 0x3ff];
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
	uint16_t val = 0;
	uint8_t region = addr >> 24;
	switch (region) {
		case 0x00:
			if (aligned < 0x4000) val = *(uint16_t*)(bus->bios + aligned);
			break;
		case 0x02:
			val = *(uint16_t*)(bus->ewram + (aligned & 0x3ffff));
			break;
		case 0x03:
			val = *(uint16_t*)(bus->iwram + (aligned & 0x7fff));
			break;
		case 0x04:
			switch (aligned & 0xffffff) {
				case 0x000000 ... 0x00005f:
					val = ppu_read16(bus->ppu, aligned);
					break;
				case 0x000060 ... 0x0000af:
					val = apu_read16(bus->apu, aligned);
					break;
				case 0x0000b0 ... 0x0000df:
					val = dma_read16(bus->dma, aligned);
					break;
				case 0x000100 ... 0x00010f:
					val = timer_read16(bus->timers, aligned);
					break;
				case 0x000130: val = host_get_controller(); break;
				case 0x000132: val = bus->keycnt; break;
				case 0x000200: val = bus->io_ie; break;
				case 0x000202: val = bus->io_if; break;
				case 0x000208: val = bus->io_ime; break;
				default:
					return 0;
			}
			break;
		case 0x05:
			val = *(uint16_t*)(bus->ppu->pram + (aligned & 0x3ff));
			break;
		case 0x06: {
			uint32_t off = aligned & 0x1ffff;
			if (off >= 0x18000) off -= 0x8000;
			val = *(uint16_t*)(bus->ppu->vram + off);
			break;
		}
		case 0x07:
			val = *(uint16_t*)(bus->ppu->oam + (aligned & 0x3ff));
			break;
		case 0x08 ... 0x0d: {
			uint32_t rom_addr = aligned & 0x01ffffff;
			if (rom_addr + 1 < bus->cart->rom_size) val = *(uint16_t*)(bus->cart->rom + rom_addr);
			break;
		}
		case 0x0e: case 0x0f:
			val = (uint16_t)cart_read(bus->cart, addr) * 0x0101;
			return val;
		default:
			return 0;
	}
	return val;
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
		case 0x04:
			switch (aligned & 0xffffff) {
				case 0x000000 ... 0x00005f:
					val = ppu_read32(bus->ppu, aligned);
					break;
				case 0x000060 ... 0x0000af:
					val = apu_read32(bus->apu, aligned);
					break;
				case 0x0000b0 ... 0x0000df:
					val = dma_read32(bus->dma, aligned);
					break;
				case 0x000100 ... 0x00010f:
					val = timer_read32(bus->timers, aligned);
					break;
				case 0x000130: val = (uint32_t)host_get_controller() | ((uint32_t)bus->keycnt << 16); break;
				case 0x000200: val = (uint32_t)bus->io_ie | ((uint32_t)bus->io_if << 16); break;
				case 0x000208: val = bus->io_ime; break;
				default:
					return 0;
			}
			break;
		case 0x05:
			val = *(uint32_t*)(bus->ppu->pram + (aligned & 0x3ff));
			break;
		case 0x06: {
			uint32_t off = aligned & 0x1ffff;
			if (off >= 0x18000) off -= 0x8000;
			val = *(uint32_t*)(bus->ppu->vram + off);
			break;
		}
		case 0x07:
			val = *(uint32_t*)(bus->ppu->oam + (aligned & 0x3ff));
			break;
		case 0x08 ... 0x0d: {
			uint32_t rom_addr = aligned & 0x01ffffff;
			if (rom_addr + 3 < bus->cart->rom_size) val = *(uint32_t*)(bus->cart->rom + rom_addr);
			break;
		}
		case 0x0e: case 0x0f:
			return (uint32_t)cart_read(bus->cart, addr) * 0x01010101;
		default:
			break;
	}

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
		case 0x04:
			switch (addr & 0xffffff) {
				case 0x000000 ... 0x00005f:
					ppu_write8(bus->ppu, addr, val);
					break;
				case 0x000060 ... 0x0000af:
					apu_write8(bus->apu, addr, val);
					break;
				case 0x0000b0 ... 0x0000df:
					dma_write8(bus->dma, addr, val);
					break;
				case 0x000100 ... 0x00010f:
					timer_write8(bus->timers, addr, val);
					break;
				case 0x000132: bus->keycnt = (bus->keycnt & 0xff00) | val; break;
				case 0x000133: bus->keycnt = (bus->keycnt & 0x00ff) | ((uint16_t)val << 8); break;
				case 0x000200: bus->io_ie = (bus->io_ie & 0xff00) | val; break;
				case 0x000201: bus->io_ie = (bus->io_ie & 0x00ff) | ((uint16_t)val << 8); break;
				case 0x000202: bus->io_if &= ~val; break;
				case 0x000203: bus->io_if &= ~((uint16_t)val << 8); break;
				case 0x000208: bus->io_ime = val & 1; break;
				case 0x000301: bus->halted = true; break;
				default:
					break;
			}
			break;
		case 0x05: {
			uint32_t off = addr & 0x3fe;
			bus->ppu->pram[off] = val;
			bus->ppu->pram[off + 1] = val;
			break;
		}
		case 0x06: {
			uint32_t off = addr & 0x1ffff;
			if (off >= 0x18000) off -= 0x8000;
			uint32_t obj_boundary = ((bus->ppu->dispcnt & 0x7) >= 3) ? 0x14000 : 0x10000;
			if (off < obj_boundary) {
				uint32_t aligned = off & ~1;
				bus->ppu->vram[aligned] = val;
				bus->ppu->vram[aligned + 1] = val;
			}
			break;
		}
		case 0x07:
			break;
		case 0x0e: case 0x0f:
			cart_write(bus->cart, addr, val);
			break;
		default:
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
		case 0x04:
			switch (aligned & 0xffffff) {
				case 0x000000 ... 0x00005f:
					ppu_write16(bus->ppu, aligned, val);
					break;
				case 0x000060 ... 0x0000af:
					apu_write16(bus->apu, aligned, val);
					break;
				case 0x0000b0 ... 0x0000df:
					dma_write16(bus->dma, aligned, val);
					break;
				case 0x000100 ... 0x00010f:
					timer_write16(bus->timers, aligned, val);
					break;
				case 0x000132: bus->keycnt = val; break;
				case 0x000200: bus->io_ie = val; break;
				case 0x000202: bus->io_if &= ~val; break;
				case 0x000208: bus->io_ime = val & 1; break;
				default:
					break;
			}
			break;
		case 0x05:
			*(uint16_t*)(bus->ppu->pram + (aligned & 0x3ff)) = val;
			break;
		case 0x06: {
			uint32_t off = aligned & 0x1ffff;
			if (off >= 0x18000) off -= 0x8000;
			*(uint16_t*)(bus->ppu->vram + off) = val;
			break;
		}
		case 0x07:
			*(uint16_t*)(bus->ppu->oam + (aligned & 0x3ff)) = val;
			break;
		case 0x0e: case 0x0f:
			cart_write(bus->cart, addr, (uint8_t)val);
			break;
		default:
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
		case 0x04:
			switch (aligned & 0xffffff) {
				case 0x000000 ... 0x00005f:
					ppu_write32(bus->ppu, aligned, val);
					break;
				case 0x000060 ... 0x0000af:
					apu_write32(bus->apu, aligned, val);
					break;
				case 0x0000b0 ... 0x0000df:
					dma_write32(bus->dma, aligned, val);
					break;
				case 0x000100 ... 0x00010f:
					timer_write32(bus->timers, aligned, val);
					break;
				case 0x000200:
					bus->io_ie = val & 0xffff;
					bus->io_if &= ~(val >> 16);
					break;
				case 0x000208:
					bus->io_ime = val & 1;
					break;
				default:
					break;
			}
			break;
		case 0x05:
			*(uint32_t*)(bus->ppu->pram + (aligned & 0x3ff)) = val;
			break;
		case 0x06: {
			uint32_t off = aligned & 0x1ffff;
			if (off >= 0x18000) off -= 0x8000;
			*(uint32_t*)(bus->ppu->vram + off) = val;
			break;
		}
		case 0x07:
			*(uint32_t*)(bus->ppu->oam + (aligned & 0x3ff)) = val;
			break;
		case 0x0e: case 0x0f:
			cart_write(bus->cart, addr, (uint8_t)val);
			break;
		default:
			break;
	}
}
