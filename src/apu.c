#include "apu.h"
#include "host.h"

#include <string.h>

static const uint8_t DUTY_TABLE[4][8] = {
	{ 0, 0, 0, 0, 0, 0, 0, 1 }, // 12.5%
	{ 1, 0, 0, 0, 0, 0, 0, 1 }, // 25%
	{ 1, 0, 0, 0, 0, 1, 1, 1 }, // 50%
	{ 0, 1, 1, 1, 1, 1, 1, 0 }, // 75%
};

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

static void trigger_ch1(APU *apu) {
	apu->ch1.enabled = true;
	if (apu->ch1.length == 0) apu->ch1.length = 64;

	uint16_t freq = apu->regs[0x64 - 0x60] | ((apu->regs[0x65 - 0x60] & 7) << 8);
	apu->ch1.timer = (2048 - freq) * 16;
	apu->ch1.volume = (apu->regs[0x63 - 0x60] >> 4) & 0xf;
	apu->ch1.env_timer = apu->regs[0x63 - 0x60] & 7;

	apu->ch1.shadow_freq = freq;
	int sweep_time = (apu->regs[0x60 - 0x60] >> 4) & 7;
	apu->ch1.sweep_timer = sweep_time ? sweep_time : 8;

	int sweep_shift = apu->regs[0x60 - 0x60] & 7;
	if (sweep_shift > 0) {
		int delta = apu->ch1.shadow_freq >> sweep_shift;
		int new_freq = (apu->regs[0x60 - 0x60] & (1 << 3)) ? (apu->ch1.shadow_freq - delta) : (apu->ch1.shadow_freq + delta);
		if (new_freq > 2047) apu->ch1.enabled = false;
	}

	if ((apu->regs[0x63 - 0x60] & 0xf8) == 0) {
		apu->ch1.enabled = false;
	}
}

static void trigger_ch2(APU *apu) {
	apu->ch2.enabled = true;
	if (apu->ch2.length == 0) apu->ch2.length = 64;

	uint16_t freq = apu->regs[0x6c - 0x60] | ((apu->regs[0x6d - 0x60] & 7) << 8);
	apu->ch2.timer = (2048 - freq) * 16;
	apu->ch2.volume = (apu->regs[0x69 - 0x60] >> 4) & 0xf;
	apu->ch2.env_timer = apu->regs[0x69 - 0x60] & 7;

	if ((apu->regs[0x69 - 0x60] & 0xf8) == 0) {
		apu->ch2.enabled = false;
	}
}

static void trigger_ch3(APU *apu) {
	if (apu->regs[0x70 - 0x60] & 0x80) {
		apu->ch3.enabled = true;
	}
	if (apu->ch3.length == 0) apu->ch3.length = 256;

	uint16_t freq = apu->regs[0x74 - 0x60] | ((apu->regs[0x75 - 0x60] & 7) << 8);
	apu->ch3.timer = (2048 - freq) * 8;
	apu->ch3.sample_pos = 0;
}

static void trigger_ch4(APU *apu) {
	apu->ch4.enabled = true;
	if (apu->ch4.length == 0) apu->ch4.length = 64;

	int r = apu->regs[0x7c - 0x60] & 7;
	int s = (apu->regs[0x7c - 0x60] >> 4) & 0xf;
	uint32_t div = (r == 0) ? 8 : (r * 16);
	apu->ch4.timer = div << (s + 2);
	if (apu->ch4.timer == 0) apu->ch4.timer = 8;

	apu->ch4.volume = (apu->regs[0x79 - 0x60] >> 4) & 0xf;
	apu->ch4.env_timer = apu->regs[0x79 - 0x60] & 7;
	apu->ch4.lfsr = 0x7fff;

	if ((apu->regs[0x79 - 0x60] & 0xf8) == 0) {
		apu->ch4.enabled = false;
	}
}

static void clock_length(APU *apu) {
	if ((apu->regs[0x65 - 0x60] & (1 << 6)) && apu->ch1.length > 0) {
		if (--apu->ch1.length == 0) apu->ch1.enabled = false;
	}
	if ((apu->regs[0x6d - 0x60] & (1 << 6)) && apu->ch2.length > 0) {
		if (--apu->ch2.length == 0) apu->ch2.enabled = false;
	}
	if ((apu->regs[0x75 - 0x60] & (1 << 6)) && apu->ch3.length > 0) {
		if (--apu->ch3.length == 0) apu->ch3.enabled = false;
	}
	if ((apu->regs[0x7d - 0x60] & (1 << 6)) && apu->ch4.length > 0) {
		if (--apu->ch4.length == 0) apu->ch4.enabled = false;
	}
}

static void clock_sweep(APU *apu) {
	if (apu->ch1.sweep_timer > 0) {
		apu->ch1.sweep_timer--;
	}
	if (apu->ch1.sweep_timer == 0) {
		int sweep_time = (apu->regs[0x60 - 0x60] >> 4) & 7;
		apu->ch1.sweep_timer = sweep_time ? sweep_time : 8;

		int sweep_shift = apu->regs[0x60 - 0x60] & 7;
		if (sweep_time > 0) {
			int delta = apu->ch1.shadow_freq >> sweep_shift;
			int new_freq = (apu->regs[0x60 - 0x60] & (1 << 3)) ? (apu->ch1.shadow_freq - delta) : (apu->ch1.shadow_freq + delta);
			if (new_freq > 2047) {
				apu->ch1.enabled = false;
			} else if (sweep_shift > 0) {
				apu->ch1.shadow_freq = new_freq;
				apu->regs[0x64 - 0x60] = new_freq & 0xff;
				apu->regs[0x65 - 0x60] = (apu->regs[0x65 - 0x60] & ~7) | ((new_freq >> 8) & 7);

				delta = new_freq >> sweep_shift;
				int next_freq = (apu->regs[0x60 - 0x60] & (1 << 3)) ? (new_freq - delta) : (new_freq + delta);
				if (next_freq > 2047) apu->ch1.enabled = false;
			}
		}
	}
}

static void clock_envelope(APU *apu) {
	int step1 = apu->regs[0x63 - 0x60] & 7;
	if (step1 > 0 && --apu->ch1.env_timer <= 0) {
		apu->ch1.env_timer = step1;
		bool dir = (apu->regs[0x63 - 0x60] >> 3) & 1;
		if (dir && apu->ch1.volume < 15) apu->ch1.volume++;
		else if (!dir && apu->ch1.volume > 0) apu->ch1.volume--;
	}

	int step2 = apu->regs[0x69 - 0x60] & 7;
	if (step2 > 0 && --apu->ch2.env_timer <= 0) {
		apu->ch2.env_timer = step2;
		bool dir = (apu->regs[0x69 - 0x60] >> 3) & 1;
		if (dir && apu->ch2.volume < 15) apu->ch2.volume++;
		else if (!dir && apu->ch2.volume > 0) apu->ch2.volume--;
	}

	int step4 = apu->regs[0x79 - 0x60] & 7;
	if (step4 > 0 && --apu->ch4.env_timer <= 0) {
		apu->ch4.env_timer = step4;
		bool dir = (apu->regs[0x79 - 0x60] >> 3) & 1;
		if (dir && apu->ch4.volume < 15) apu->ch4.volume++;
		else if (!dir && apu->ch4.volume > 0) apu->ch4.volume--;
	}
}

uint8_t apu_read8(APU *apu, uint8_t addr) {
	switch (addr) {
		case 0xa0 ... 0xa7:
			return 0;
		case 0x84: {
			uint8_t status = apu->regs[0x84 - 0x60] & 0x80;
			if (apu->ch1.enabled) status |= 0x01;
			if (apu->ch2.enabled) status |= 0x02;
			if (apu->ch3.enabled) status |= 0x04;
			if (apu->ch4.enabled) status |= 0x08;
			return status;
		}
		case 0x90 ... 0x9f: {
			uint8_t bank = 1 - ((apu->regs[0x70 - 0x60] >> 6) & 1);
			return apu->wave_ram[bank][addr - 0x90];
		}
		case 0x60 ... 0x83:
		case 0x85 ... 0x8f:
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
			return;
		case 0xa4 ... 0xa7:
			fifo_b_push(apu, val);
			return;
		case 0x83:
			if (val & (1 << 3)) apu->fifo_a_len = 0;
			if (val & (1 << 7)) apu->fifo_b_len = 0;
			apu->regs[0x83 - 0x60] = val & 0x77;
			return;
		case 0x84:
			if (!(val & 0x80)) {
				memset(&apu->regs[0], 0, 0x82 - 0x60);
				apu->ch1.enabled = false;
				apu->ch2.enabled = false;
				apu->ch3.enabled = false;
				apu->ch4.enabled = false;
				apu->regs[0x84 - 0x60] = 0;
			} else {
				apu->regs[0x84 - 0x60] |= 0x80;
			}
			return;
		case 0x90 ... 0x9f: {
			uint8_t bank = 1 - ((apu->regs[0x70 - 0x60] >> 6) & 1);
			apu->wave_ram[bank][addr - 0x90] = val;
			apu->regs[addr - 0x60] = val;
			return;
		}
		case 0x60 ... 0x82:
		case 0x85 ... 0x8f:
		case 0xa8 ... 0xaf:
			apu->regs[addr - 0x60] = val;
			break;
		default:
			return;
	}

	switch (addr) {
		case 0x62: apu->ch1.length = 64 - (val & 0x3f); break;
		case 0x65: if (val & (1 << 7)) trigger_ch1(apu); break;
		case 0x68: apu->ch2.length = 64 - (val & 0x3f); break;
		case 0x6d: if (val & (1 << 7)) trigger_ch2(apu); break;
		case 0x70: if (!(val & 0x80)) apu->ch3.enabled = false; break;
		case 0x72: apu->ch3.length = 256 - val; break;
		case 0x75: if (val & (1 << 7)) trigger_ch3(apu); break;
		case 0x78: apu->ch4.length = 64 - (val & 0x3f); break;
		case 0x7d: if (val & (1 << 7)) trigger_ch4(apu); break;
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

	memset(&apu->ch1, 0, sizeof(apu->ch1));
	memset(&apu->ch2, 0, sizeof(apu->ch2));
	memset(&apu->ch3, 0, sizeof(apu->ch3));
	memset(&apu->ch4, 0, sizeof(apu->ch4));
	memset(apu->wave_ram, 0, sizeof(apu->wave_ram));

	apu->ch4.lfsr = 0x7fff;
	apu->frame_seq_step = 0;

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

	if (apu->ch1.enabled) {
		if (--apu->ch1.timer <= 0) {
			uint16_t freq = apu->regs[0x64 - 0x60] | ((apu->regs[0x65 - 0x60] & 7) << 8);
			apu->ch1.timer = (2048 - freq) * 16;
			apu->ch1.duty_step = (apu->ch1.duty_step + 1) & 7;
		}
	}
	if (apu->ch2.enabled) {
		if (--apu->ch2.timer <= 0) {
			uint16_t freq = apu->regs[0x6c - 0x60] | ((apu->regs[0x6d - 0x60] & 7) << 8);
			apu->ch2.timer = (2048 - freq) * 16;
			apu->ch2.duty_step = (apu->ch2.duty_step + 1) & 7;
		}
	}
	if (apu->ch3.enabled) {
		if (--apu->ch3.timer <= 0) {
			uint16_t freq = apu->regs[0x74 - 0x60] | ((apu->regs[0x75 - 0x60] & 7) << 8);
			apu->ch3.timer = (2048 - freq) * 8;
			int max = (apu->regs[0x70 - 0x60] & (1 << 5)) ? 64 : 32;
			apu->ch3.sample_pos = (apu->ch3.sample_pos + 1) % max;
		}
	}
	if (apu->ch4.enabled) {
		if (--apu->ch4.timer <= 0) {
			int r = apu->regs[0x7c - 0x60] & 7;
			int s = (apu->regs[0x7c - 0x60] >> 4) & 0xf;
			uint32_t div = (r == 0) ? 8 : (r * 16);
			apu->ch4.timer = div << (s + 2);
			if (apu->ch4.timer == 0) apu->ch4.timer = 8;

			uint16_t res = (apu->ch4.lfsr & 1) ^ ((apu->ch4.lfsr >> 1) & 1);
			apu->ch4.lfsr = (apu->ch4.lfsr >> 1) | (res << 14);
			if (apu->regs[0x7c - 0x60] & (1 << 3)) {
				apu->ch4.lfsr = (apu->ch4.lfsr & ~(1 << 6)) | (res << 6);
			}
		}
	}

	if ((apu->cycles & 32767) == 0) {
		switch (apu->frame_seq_step) {
			case 0:
				clock_length(apu);
				break;
			case 2:
				clock_length(apu);
				clock_sweep(apu);
				break;
			case 4:
				clock_length(apu);
				break;
			case 6:
				clock_length(apu);
				clock_sweep(apu);
				break;
			case 7:
				clock_envelope(apu);
				break;
		}
		apu->frame_seq_step = (apu->frame_seq_step + 1) & 7;
	}

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

			// Game Boy channels
			int duty1 = (apu->regs[0x62 - 0x60] >> 6) & 3;
			int32_t ch1_s = (apu->ch1.enabled && DUTY_TABLE[duty1][apu->ch1.duty_step])
				? (int32_t)apu->ch1.volume : -(int32_t)apu->ch1.volume;
			if (!apu->ch1.enabled) ch1_s = 0;

			int duty2 = (apu->regs[0x68 - 0x60] >> 6) & 3;
			int32_t ch2_s = (apu->ch2.enabled && DUTY_TABLE[duty2][apu->ch2.duty_step])
				? (int32_t)apu->ch2.volume : -(int32_t)apu->ch2.volume;
			if (!apu->ch2.enabled) ch2_s = 0;

			int32_t ch3_s = 0;
			if (apu->ch3.enabled && (apu->regs[0x70 - 0x60] & 0x80)) {
				int bank = (apu->regs[0x70 - 0x60] >> 6) & 1;
				if (apu->regs[0x70 - 0x60] & (1 << 5)) bank ^= (apu->ch3.sample_pos >= 32);
				int pos = apu->ch3.sample_pos & 31;
				uint8_t byte = apu->wave_ram[bank][pos / 2];
				int raw = (pos & 1) ? (byte & 0xf) : (byte >> 4);
				int centered = raw - 8;
				int vol_sel = (apu->regs[0x73 - 0x60] >> 5) & 3;
				if (apu->regs[0x73 - 0x60] & 0x80) ch3_s = (centered * 3) >> 2;
				else if (vol_sel == 0) ch3_s = 0;
				else ch3_s = centered >> (vol_sel - 1);
			}

			int32_t ch4_s = (apu->ch4.enabled && !(apu->ch4.lfsr & 1))
				? (int32_t)apu->ch4.volume : -(int32_t)apu->ch4.volume;
			if (!apu->ch4.enabled) ch4_s = 0;

			uint16_t soundcnt_l = apu->regs[0x80 - 0x60] | (apu->regs[0x81 - 0x60] << 8);
			int32_t psg_left = 0;
			int32_t psg_right = 0;

			if (soundcnt_l & (1 << 12)) psg_left += ch1_s;
			if (soundcnt_l & (1 << 13)) psg_left += ch2_s;
			if (soundcnt_l & (1 << 14)) psg_left += ch3_s;
			if (soundcnt_l & (1 << 15)) psg_left += ch4_s;

			if (soundcnt_l & (1 << 8))  psg_right += ch1_s;
			if (soundcnt_l & (1 << 9))  psg_right += ch2_s;
			if (soundcnt_l & (1 << 10)) psg_right += ch3_s;
			if (soundcnt_l & (1 << 11)) psg_right += ch4_s;

			psg_left *= ((soundcnt_l >> 4) & 7) + 1;
			psg_right *= (soundcnt_l & 7) + 1;

			int psg_ratio = soundcnt_h & 3;
			if (psg_ratio == 0) {
				psg_left >>= 2;
				psg_right >>= 2;
			} else if (psg_ratio == 1) {
				psg_left >>= 1;
				psg_right >>= 1;
			}

			left += psg_left << 5;
			right += psg_right << 5;

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
