#include "timer.h"
#include "bus.h"
#include "apu.h"

#include <string.h>

static const uint32_t PRESCALER_DIVS[4] = { 1, 64, 256, 1024 };

void timer_init(Timers *timers, Bus *bus) {
	timers->bus = bus;
	timer_reset(timers);
}

void timer_reset(Timers *timers) {
	memset(timers->timer, 0, sizeof(timers->timer));
}

static void timer_tick(Timers *timers, int i) {
	Timer *t = &timers->timer[i];
	t->counter++;
	if (t->counter == 0) {
		t->counter = t->reload;

		if (t->cnt_h & (1 << 6)) {
			timers->bus->io_if |= (1 << (3 + i));
		}

		if (i == 0 || i == 1) {
			apu_timer_overflow(timers->bus->apu, i);
		}

		if (i < 3) {
			Timer *next = &timers->timer[i + 1];
			if ((next->cnt_h & (1 << 7)) && (next->cnt_h & (1 << 2))) {
				timer_tick(timers, i + 1);
			}
		}
	}
}

void timer_clock(Timers *timers) {
	for (int i = 0; i < 4; i++) {
		Timer *t = &timers->timer[i];
		if (!(t->cnt_h & (1 << 7))) {
			continue;
		}

		if ((i > 0) && (t->cnt_h & (1 << 2))) {
			continue;
		}

		uint8_t prescaler_sel = t->cnt_h & 3;
		uint32_t div = PRESCALER_DIVS[prescaler_sel];

		t->prescaler_count++;
		if (t->prescaler_count >= div) {
			t->prescaler_count = 0;
			timer_tick(timers, i);
		}
	}
}

uint8_t timer_read8(Timers *timers, uint8_t addr) {
	uint16_t val = timer_read16(timers, addr & ~1);
	return (addr & 1) ? (val >> 8) : (val & 0xff);
}

uint16_t timer_read16(Timers *timers, uint8_t addr) {
	int i = (addr >> 2) & 3;
	int off = addr & 3;
	if (off == 0) {
		return timers->timer[i].counter;
	} else if (off == 2) {
		return timers->timer[i].cnt_h;
	}
	return 0;
}

uint32_t timer_read32(Timers *timers, uint8_t addr) {
	return (uint32_t)timer_read16(timers, addr) | ((uint32_t)timer_read16(timers, addr + 2) << 16);
}

void timer_write8(Timers *timers, uint8_t addr, uint8_t val) {
	uint16_t curr = timer_read16(timers, addr & ~1);
	if (addr & 1) {
		curr = (curr & 0x00ff) | ((uint16_t)val << 8);
	} else {
		curr = (curr & 0xff00) | val;
	}
	timer_write16(timers, addr & ~1, curr);
}

void timer_write16(Timers *timers, uint8_t addr, uint16_t val) {
	int i = (addr >> 2) & 3;
	int off = addr & 3;
	Timer *t = &timers->timer[i];

	if (off == 0) {
		t->reload = val;
	} else if (off == 2) {
		bool old_enable = (t->cnt_h >> 7) & 1;
		bool new_enable = (val >> 7) & 1;
		t->cnt_h = val;
		if (!old_enable && new_enable) {
			t->counter = t->reload;
			t->prescaler_count = 0;
		}
	}
}

void timer_write32(Timers *timers, uint8_t addr, uint32_t val) {
	timer_write16(timers, addr, (uint16_t)val);
	timer_write16(timers, addr + 2, (uint16_t)(val >> 16));
}
