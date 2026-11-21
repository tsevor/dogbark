#include "dma.h"
#include "bus.h"
#include "apu.h"

#include <string.h>

void dma_init(DMA *dma, Bus *bus) {
	dma->bus = bus;
	dma_reset(dma);
}

void dma_reset(DMA *dma) {
	memset(dma->chan, 0, sizeof(dma->chan));
}

static void dma_run_channel(DMA *dma, int ch_id) {
	DMAChannel *ch = &dma->chan[ch_id];
	bool is_32 = (ch->cnt_h >> 10) & 1;
	uint32_t step = is_32 ? 4 : 2;
	int dst_ctl = (ch->cnt_h >> 5) & 3;
	int src_ctl = (ch->cnt_h >> 7) & 3;
	uint32_t count = ch->current_count;

	if (count == 0) {
		count = (ch_id == 3) ? 0x10000 : 0x4000;
	}

	uint32_t sad_mask = (ch_id == 0) ? 0x07ffffff : 0x0fffffff;
	uint32_t dad_mask = (ch_id == 3) ? 0x0fffffff : 0x07ffffff;
	sad_mask &= is_32 ? 0xfffffffc : 0xfffffffe;
	dad_mask &= is_32 ? 0xfffffffc : 0xfffffffe;

	for (uint32_t i = 0; i < count; i++) {
		if (is_32) {
			uint32_t val = bus_read32(dma->bus, ch->current_sad & sad_mask);
			bus_write32(dma->bus, ch->current_dad & dad_mask, val);
		} else {
			uint16_t val = bus_read16(dma->bus, ch->current_sad & sad_mask);
			bus_write16(dma->bus, ch->current_dad & dad_mask, val);
		}

		if (src_ctl == 0) ch->current_sad += step;
		else if (src_ctl == 1) ch->current_sad -= step;

		if (dst_ctl == 0 || dst_ctl == 3) ch->current_dad += step;
		else if (dst_ctl == 1) ch->current_dad -= step;
	}

	if (ch->cnt_h & (1 << 14)) {
		dma->bus->io_if |= (1 << (8 + ch_id));
	}

	bool repeat = (ch->cnt_h >> 9) & 1;
	uint8_t timing = (ch->cnt_h >> 12) & 3;

	if (repeat && timing != 0) {
		if (dst_ctl == 3) {
			ch->current_dad = ch->dad;
		}
		uint32_t max_count = (ch_id == 3) ? 0x10000 : 0x4000;
		uint32_t masked_count = ch->count & (max_count - 1);
		ch->current_count = masked_count ? masked_count : max_count;
	} else {
		ch->cnt_h &= ~0x8000;
		ch->active = false;
	}
}

void dma_clock(DMA *dma) {
	if (dma->bus->apu->dma_req_a) {
		DMAChannel *ch = &dma->chan[1];
		if (ch->active && ((ch->cnt_h >> 12) & 3) == 3) {
			for (int i = 0; i < 4; i++) {
				uint32_t val = bus_read32(dma->bus, ch->current_sad & 0x0fffffff);
				bus_write32(dma->bus, 0x040000a0, val);
				ch->current_sad += 4;
			}
		}
		dma->bus->apu->dma_req_a = false;
	}

	if (dma->bus->apu->dma_req_b) {
		DMAChannel *ch = &dma->chan[2];
		if (ch->active && ((ch->cnt_h >> 12) & 3) == 3) {
			for (int i = 0; i < 4; i++) {
				uint32_t val = bus_read32(dma->bus, ch->current_sad & 0x0fffffff);
				bus_write32(dma->bus, 0x040000a4, val);
				ch->current_sad += 4;
			}
		}
		dma->bus->apu->dma_req_b = false;
	}
}

void dma_check_vblank(DMA *dma) {
	for (int i = 0; i < 4; i++) {
		DMAChannel *ch = &dma->chan[i];
		if (ch->active && ((ch->cnt_h >> 12) & 3) == 1) {
			dma_run_channel(dma, i);
		}
	}
}

void dma_check_hblank(DMA *dma) {
	for (int i = 0; i < 4; i++) {
		DMAChannel *ch = &dma->chan[i];
		if (ch->active && ((ch->cnt_h >> 12) & 3) == 2) {
			dma_run_channel(dma, i);
		}
	}
}

uint8_t dma_read8(DMA *dma, uint8_t addr) {
	uint16_t val = dma_read16(dma, addr & ~1);
	return (addr & 1) ? (val >> 8) : (val & 0xff);
}

uint16_t dma_read16(DMA *dma, uint8_t addr) {
	if (addr < 0xb0 || addr >= 0xe0) return 0;
	int ch_id = (addr - 0xb0) / 12;
	int off = (addr - 0xb0) % 12;
	if (off == 10) {
		return dma->chan[ch_id].cnt_h;
	}
	return 0;
}

uint32_t dma_read32(DMA *dma, uint8_t addr) {
	return (uint32_t)dma_read16(dma, addr) | ((uint32_t)dma_read16(dma, addr + 2) << 16);
}

void dma_write8(DMA *dma, uint8_t addr, uint8_t val) {
	if (addr < 0xb0 || addr >= 0xe0) return;
	int ch_id = (addr - 0xb0) / 12;
	int off = (addr - 0xb0) % 12;
	if (off == 10 || off == 11) {
		uint16_t curr = dma->chan[ch_id].cnt_h;
		if (addr & 1) curr = (curr & 0x00ff) | (val << 8);
		else curr = (curr & 0xff00) | val;
		dma_write16(dma, addr & ~1, curr);
	} else if (off >= 0 && off <= 3) {
		int shift = (off & 3) * 8;
		dma->chan[ch_id].sad = (dma->chan[ch_id].sad & ~(0xff << shift)) | (val << shift);
	} else if (off >= 4 && off <= 7) {
		int shift = (off & 3) * 8;
		dma->chan[ch_id].dad = (dma->chan[ch_id].dad & ~(0xff << shift)) | (val << shift);
	} else if (off == 8 || off == 9) {
		int shift = (off & 1) * 8;
		dma->chan[ch_id].count = (dma->chan[ch_id].count & ~(0xff << shift)) | (val << shift);
	}
}

void dma_write16(DMA *dma, uint8_t addr, uint16_t val) {
	if (addr < 0xb0 || addr >= 0xe0) return;
	int ch_id = (addr - 0xb0) / 12;
	int off = (addr - 0xb0) % 12;
	DMAChannel *ch = &dma->chan[ch_id];

	switch (off) {
		case 0: ch->sad = (ch->sad & 0xffff0000) | val; break;
		case 2: ch->sad = (ch->sad & 0x0000ffff) | (val << 16); break;
		case 4: ch->dad = (ch->dad & 0xffff0000) | val; break;
		case 6: ch->dad = (ch->dad & 0x0000ffff) | (val << 16); break;
		case 8: ch->count = val; break;
		case 10: {
			bool was_enabled = (ch->cnt_h >> 15) & 1;
			bool now_enabled = (val >> 15) & 1;
			ch->cnt_h = val;
			if (!was_enabled && now_enabled) {
				ch->current_sad = ch->sad;
				ch->current_dad = ch->dad;
				uint32_t max_count = (ch_id == 3) ? 0x10000 : 0x4000;
				uint32_t masked_count = ch->count & (max_count - 1);
				ch->current_count = masked_count ? masked_count : max_count;
				ch->active = true;

				uint8_t timing = (val >> 12) & 3;
				if (timing == 0) {
					dma_run_channel(dma, ch_id);
				}
			} else if (!now_enabled) {
				ch->active = false;
			}
			break;
		}
	}
}

void dma_write32(DMA *dma, uint8_t addr, uint32_t val) {
	if (addr < 0xb0 || addr >= 0xe0) return;
	int ch_id = (addr - 0xb0) / 12;
	int off = (addr - 0xb0) % 12;
	DMAChannel *ch = &dma->chan[ch_id];

	switch (off) {
		case 0:
			ch->sad = val;
			break;
		case 4:
			ch->dad = val;
			break;
		case 8:
			ch->count = val & 0xffff;
			dma_write16(dma, addr + 2, val >> 16);
			break;
		default:
			dma_write16(dma, addr, val & 0xffff);
			dma_write16(dma, addr + 2, val >> 16);
			break;
	}
}
