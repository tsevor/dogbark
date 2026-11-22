#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct Channel1 {
	bool enabled;
	int length;
	int timer;
	int duty_step;
	int volume;
	int env_timer;
	int shadow_freq;
	int sweep_timer;
} Channel1;

typedef struct Channel2 {
	bool enabled;
	int length;
	int timer;
	int duty_step;
	int volume;
	int env_timer;
} Channel2;

typedef struct Channel3 {
	bool enabled;
	int length;
	int timer;
	int sample_pos;
} Channel3;

typedef struct Channel4 {
	bool enabled;
	int length;
	int timer;
	int volume;
	int env_timer;
	uint16_t lfsr;
} Channel4;

typedef struct APU {
	uint8_t regs[0x50];
	uint8_t fifo_a[32];
	uint8_t fifo_b[32];
	int8_t fifo_a_len;
	int8_t fifo_b_len;

	int8_t sample_a;
	int8_t sample_b;

	bool dma_req_a;
	bool dma_req_b;

	uint64_t cycles;

	int16_t audio_buffer[512];
	size_t audio_buffer_len;

	Channel1 ch1;
	Channel2 ch2;
	Channel3 ch3;
	Channel4 ch4;

	uint8_t wave_ram[2][16];
	uint8_t frame_seq_step;
} APU;

uint8_t apu_read8(APU *apu, uint8_t addr);
uint16_t apu_read16(APU *apu, uint8_t addr);
uint32_t apu_read32(APU *apu, uint8_t addr);

void apu_write8(APU *apu, uint8_t addr, uint8_t val);
void apu_write16(APU *apu, uint8_t addr, uint16_t val);
void apu_write32(APU *apu, uint8_t addr, uint32_t val);

void apu_init(APU *apu);
void apu_reset(APU *apu);

void apu_timer_overflow(APU *apu, int timer_id);
void apu_clock(APU *apu);
