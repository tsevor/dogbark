#include "ppu.h"

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

void ppu_clock(PPU *ppu) {
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
