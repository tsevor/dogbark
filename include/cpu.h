#pragma once

#include "bus.h"

#include <stdint.h>

typedef enum {
	OPMODE_USER = 0x10,
	OPMODE_FIQ  = 0x11,
	OPMODE_IRQ  = 0x12,
	OPMODE_SWI  = 0x13,
	OPMODE_ABT  = 0x17,
	OPMODE_UND  = 0x1b,
	OPMODE_SYS  = 0x1f,
} OperatingMode;


typedef struct CPU {
	union {
		uint32_t regs[16];
		struct {
			uint32_t r[13];
			uint32_t sp;
			uint32_t lr;
			uint32_t pc;
		};
	};
	uint32_t regs_bak[16];

	union {
		uint32_t cpsr;
		struct {
			OperatingMode mode : 5;
			uint32_t t : 1; // 0=arm, 1=thumb
			uint32_t f : 1; // fiq disable
			uint32_t i : 1; // irq disable
			uint32_t : 16;  // reserved + 2 arm11 only
			uint32_t : 3;   // reserved + j flag
			uint32_t q : 1; // sticky overflow
			uint32_t v : 1; // overflow flag
			uint32_t c : 1; // carry flag
			uint32_t z : 1; // zero flag
			uint32_t n : 1; // sign flag
		};
	};

	union {
		uint32_t spsr;
		struct {
			OperatingMode s_mode : 5;
			uint32_t s_t : 1; // 0=arm, 1=thumb
			uint32_t s_f : 1; // fiq disable
			uint32_t s_i : 1; // irq disable
			uint32_t : 16;    // reserved + 2 arm11 only
			uint32_t : 3;     // reserved + j flag
			uint32_t s_q : 1; // sticky overflow
			uint32_t s_v : 1; // overflow flag
			uint32_t s_c : 1; // carry flag
			uint32_t s_z : 1; // zero flag
			uint32_t s_n : 1; // sign flag
		};
	};


	Bus *bus;
} CPU;

void cpu_init(CPU *cpu);
void cpu_reset(CPU *cpu);

void cpu_run(CPU *cpu);
