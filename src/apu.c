#include "apu.h"
#include "host.h"

#include <string.h>

static void fifo_a_push(APU *apu, uint8_t byte) {
	if (apu->fifo_a_len < 32) {
		apu->fifo_a[apu->fifo_a_len++] = byte;
	}
}

static void fifo_b_push(APU *apu, uint8_t byte) {
	if (apu->fifo_b_len < 32) {
		apu->fifo_b[apu->fifo_b_len++] = byte;
	}
}

static int8_t fifo_a_pop(APU *apu) {
	if (apu->fifo_a_len <= 0) return 0;
	int8_t sample = (int8_t)apu->fifo_a[0];
	apu->fifo_a_len--;
	memmove(&apu->fifo_a[0], &apu->fifo_a[1], apu->fifo_a_len);
	return sample;
}

static int8_t fifo_b_pop(APU *apu) {
	if (apu->fifo_b_len <= 0) return 0;
	int8_t sample = (int8_t)apu->fifo_b[0];
	apu->fifo_b_len--;
	memmove(&apu->fifo_b[0], &apu->fifo_b[1], apu->fifo_b_len);
	return sample;
}

uint8_t apu_read8(APU *apu, uint8_t addr) {
	switch (addr) {
		case 0xa0 ... 0xa7:
			return 0;
		case 0x60 ... 0x9f:
		case 0xa8 ... 0xaf:
			return apu->regs[addr - 0x60];
		default:
			return 0;
	}
}

uint16_t apu_read16(APU *apu, uint8_t addr) {
	switch (addr) {
		case 0xa0 ... 0xa7:
			return 0;
		default:
			return apu_read8(apu, addr) | (apu_read8(apu, addr + 1) << 8);
	}
}

uint32_t apu_read32(APU *apu, uint8_t addr) {
	switch (addr) {
		case 0xa0:
		case 0xa4:
			return 0;
		default:
			return apu_read16(apu, addr) | (apu_read16(apu, addr + 2) << 16);
	}
}

void apu_write8(APU *apu, uint8_t addr, uint8_t val) {
	switch (addr) {
		case 0xa0 ... 0xa3:
			fifo_a_push(apu, val);
			break;
		case 0xa4 ... 0xa7:
			fifo_b_push(apu, val);
			break;
		case 0x83:
			if (val & (1 << 3)) apu->fifo_a_len = 0;
			if (val & (1 << 7)) apu->fifo_b_len = 0;
			apu->regs[0x83 - 0x60] = val & 0x77;
			break;
		case 0x60 ... 0x82:
		case 0x84 ... 0x9f:
		case 0xa8 ... 0xaf:
			apu->regs[addr - 0x60] = val;
			break;
		default:
			break;
	}
}

void apu_write16(APU *apu, uint8_t addr, uint16_t val) {
	switch (addr) {
		case 0xa0:
			fifo_a_push(apu, val);
			fifo_a_push(apu, val >> 8);
			break;
		case 0xa4:
			fifo_b_push(apu, val);
			fifo_b_push(apu, val >> 8);
			break;
		case 0x82:
			if (val & (1 << 11)) apu->fifo_a_len = 0;
			if (val & (1 << 15)) apu->fifo_b_len = 0;
			apu->regs[0x82 - 0x60] = val;
			apu->regs[0x83 - 0x60] = (val >> 8) & 0x77;
			break;
		default:
			apu_write8(apu, addr, val);
			apu_write8(apu, addr + 1, val >> 8);
			break;
	}
}

void apu_write32(APU *apu, uint8_t addr, uint32_t val) {
	switch (addr) {
		case 0xa0:
			fifo_a_push(apu, val);
			fifo_a_push(apu, (val >> 8));
			fifo_a_push(apu, (val >> 16));
			fifo_a_push(apu, (val >> 24));
			break;
		case 0xa4:
			fifo_b_push(apu, val);
			fifo_b_push(apu, (val >> 8));
			fifo_b_push(apu, (val >> 16));
			fifo_b_push(apu, (val >> 24));
			break;
		default:
			apu_write16(apu, addr, val);
			apu_write16(apu, addr + 2, val >> 16);
			break;
	}
}

void apu_init(APU *apu) {
	apu_reset(apu);
}

void apu_reset(APU *apu) {
	memset(apu->regs, 0, sizeof(apu->regs));
	memset(apu->fifo_a, 0, sizeof(apu->fifo_a));
	memset(apu->fifo_b, 0, sizeof(apu->fifo_b));
	apu->fifo_a_len = 0;
	apu->fifo_b_len = 0;

	apu->sample_a = 0;
	apu->sample_b = 0;

	apu->dma_req_a = false;
	apu->dma_req_b = false;

	apu->cycles = 0;
	apu->audio_buffer_len = 0;

	apu->regs[0x88 - 0x60] = 0x00;
	apu->regs[0x89 - 0x60] = 0x02;
}

void apu_timer_overflow(APU *apu, int timer_id) {
	uint16_t soundcnt_h = apu->regs[0x82 - 0x60] | (apu->regs[0x83 - 0x60] << 8);

	int timer_a = (soundcnt_h >> 10) & 1;
	if (timer_id == timer_a) {
		apu->sample_a = fifo_a_pop(apu);
		if (apu->fifo_a_len <= 16) {
			apu->dma_req_a = true;
		}
	}

	int timer_b = (soundcnt_h >> 14) & 1;
	if (timer_id == timer_b) {
		apu->sample_b = fifo_b_pop(apu);
		if (apu->fifo_b_len <= 16) {
			apu->dma_req_b = true;
		}
	}
}

void apu_clock(APU *apu) {
	apu->cycles++;

	if ((apu->cycles & 511) == 0) {
		uint8_t soundcnt_x = apu->regs[0x84 - 0x60];

		if (!(soundcnt_x & 0x80)) {
			apu->audio_buffer[apu->audio_buffer_len++] = 0;
			apu->audio_buffer[apu->audio_buffer_len++] = 0;
		} else {
			uint16_t soundcnt_h = apu->regs[0x82 - 0x60] | (apu->regs[0x83 - 0x60] << 8);

			int32_t val_a = apu->sample_a;
			if (!(soundcnt_h & (1 << 2))) {
				val_a >>= 1;
			}
			val_a <<= 7;

			int32_t val_b = apu->sample_b;
			if (!(soundcnt_h & (1 << 3))) {
				val_b >>= 1;
			}
			val_b <<= 7;

			int32_t left = 0;
			int32_t right = 0;

			if (soundcnt_h & (1 << 9))  left += val_a;
			if (soundcnt_h & (1 << 8))  right += val_a;

			if (soundcnt_h & (1 << 13)) left += val_b;
			if (soundcnt_h & (1 << 12)) right += val_b;

			if (left > 32767) left = 32767;
			else if (left < -32768) left = -32768;

			if (right > 32767) right = 32767;
			else if (right < -32768) right = -32768;

			apu->audio_buffer[apu->audio_buffer_len++] = left;
			apu->audio_buffer[apu->audio_buffer_len++] = right;
		}

		if (apu->audio_buffer_len >= sizeof(apu->audio_buffer) / sizeof(apu->audio_buffer[0])) {
			host_queue_audio_buffer(apu->audio_buffer, apu->audio_buffer_len);
			apu->audio_buffer_len = 0;
		}
	}
}
