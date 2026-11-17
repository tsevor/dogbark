#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct Bus Bus;

typedef struct Timer {
	uint16_t reload;
	uint16_t counter;
	uint16_t cnt_h;
	uint32_t prescaler_count;
} Timer;

typedef struct Timers {
	Timer timer[4];
	Bus *bus;
} Timers;

void timer_init(Timers *timers, Bus *bus);
void timer_reset(Timers *timers);
void timer_clock(Timers *timers);

uint8_t timer_read8(Timers *timers, uint8_t addr);
uint16_t timer_read16(Timers *timers, uint8_t addr);
uint32_t timer_read32(Timers *timers, uint8_t addr);

void timer_write8(Timers *timers, uint8_t addr, uint8_t val);
void timer_write16(Timers *timers, uint8_t addr, uint16_t val);
void timer_write32(Timers *timers, uint8_t addr, uint32_t val);
