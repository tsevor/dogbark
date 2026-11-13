#include "cpu.h"
#include "host.h"
#include "log.h"
#include <stdlib.h>

typedef enum OpClass {
	ARM_TRANSSWP12,
	ARM_MULLONG,
	ARM_MULTIPLY,
	ARM_TRANSIMM10,
	ARM_TRANSREG10,
	ARM_BX,
	ARM_PSR_REG,
	ARM_DATAPROC,
	ARM_PSR_IMM,
	ARM_TRANSIMM9,
	ARM_UNDEFINED,
	ARM_TRANSREG9,
	ARM_BLOCKTRANS,
	ARM_BBL,
	ARM_CODATATRANS,
	ARM_SWI,
	ARM_COREGTRANS,
	ARM_CODATAOP,
} OpClass;

static OpClass OPCODE_CLASS[4096];

OpClass classify_opcode(uint32_t opcode) {
	switch ((opcode >> 25) & 0b111) {
		case 0b000:
			if ((opcode & 0x00000090) == 0x00000090) {
				if ((opcode & 0x00000060) == 0) {
					if ((opcode & 0x01000000) != 0) return ARM_TRANSSWP12;
					else if ((opcode & 0x00800000) != 0) return ARM_MULLONG;
					else return ARM_MULTIPLY;
				} else {
					if ((opcode & 0x00400000) != 0) return ARM_TRANSIMM10;
					else return ARM_TRANSREG10;
				}
			} else {
				if ((opcode & 0x0ff000f0) == 0x01200010) return ARM_BX;
				else if ((opcode & 0x0fb00000) == 0x01000000) return ARM_PSR_REG;
				else return ARM_DATAPROC;
			}
		case 0b001:
			if ((opcode & 0x0fb00000) == 0x03200000) return ARM_PSR_IMM;
			else return ARM_DATAPROC;
		case 0b010:
			return ARM_TRANSIMM9;
		case 0b011:
			if ((opcode & 0x00000010) == 0x00000010) return ARM_UNDEFINED;
			else return ARM_TRANSREG9;
		case 0b100:
			return ARM_BLOCKTRANS;
		case 0b101:
			return ARM_BBL;
		case 0b110:
			return ARM_CODATATRANS;
		case 0b111:
			if ((opcode & 0x01000000) == 0x01000000) return ARM_SWI;
			else if ((opcode & 0x00000010) == 0x00000010) return ARM_COREGTRANS;
			else return ARM_CODATAOP;
	}
	return ARM_UNDEFINED;
}

void init_opcode_lut() {
	for (int i = 0; i < 4096; i++) {
		uint32_t op = ((i & 0xff0) << 16) | ((i & 0xf) << 4);
		OPCODE_CLASS[i] = classify_opcode(op);
	}
}

static const uint16_t ARM_COND[16] = {
	0x56aa, 0x6a9a, 0x66a9, 0x6a99,
	0x55a6, 0x6996, 0x66a5, 0x6a95,
	0x6a6a, 0x565a, 0x6a69, 0x6659,
	0x6966, 0x5556, 0x6a65, 0x6655,
};

static inline bool eval_op_arm_cond(CPU *cpu, uint32_t opcode) {
	return (ARM_COND[cpu->cpsr >> 28] >> (opcode >> 28)) & 1;
}

void op_arm_transswp12(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_mullong(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_multiply(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_transimm10(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_transreg10(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_bx(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_psr_reg(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_dataproc(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_psr_imm(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_transimm9(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_transreg9(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_blocktrans(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_bbl(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_codatatrans(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_swi(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_coregtrans(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_codataop(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
}

void op_arm_undefined(CPU *cpu, uint32_t opcode) {
	lwarn("cpu: hit undefined opcode: 0x%08x\n", opcode);
	(void)cpu;
}

void cpu_exec_arm(CPU *cpu) {
	uint32_t opcode = bus_read(cpu->bus, cpu->pc);
	cpu->pc += 4;
	if (!eval_op_arm_cond(cpu, opcode)) {
		printf("cpu arm: %08x: %08x SKIPPED\n", cpu->pc - 4, opcode);
		return;
	}

	uint32_t lutidx = ((opcode >> 16) & 0x0ff0) | ((opcode >> 4) & 0x000f);

	switch (OPCODE_CLASS[lutidx]) {
		case ARM_TRANSSWP12:  op_arm_transswp12(cpu, opcode);  break;
		case ARM_MULLONG:     op_arm_mullong(cpu, opcode);     break;
		case ARM_MULTIPLY:    op_arm_multiply(cpu, opcode);    break;
		case ARM_TRANSIMM10:  op_arm_transimm10(cpu, opcode);  break;
		case ARM_TRANSREG10:  op_arm_transreg10(cpu, opcode);  break;
		case ARM_BX:          op_arm_bx(cpu, opcode);          break;
		case ARM_PSR_REG:     op_arm_psr_reg(cpu, opcode);     break;
		case ARM_DATAPROC:    op_arm_dataproc(cpu, opcode);    break;
		case ARM_PSR_IMM:     op_arm_psr_imm(cpu, opcode);     break;
		case ARM_TRANSIMM9:   op_arm_transimm9(cpu, opcode);   break;
		case ARM_TRANSREG9:   op_arm_transreg9(cpu, opcode);   break;
		case ARM_BLOCKTRANS:  op_arm_blocktrans(cpu, opcode);  break;
		case ARM_BBL:         op_arm_bbl(cpu, opcode);         break;
		case ARM_CODATATRANS: op_arm_codatatrans(cpu, opcode); break;
		case ARM_SWI:         op_arm_swi(cpu, opcode);         break;
		case ARM_COREGTRANS:  op_arm_coregtrans(cpu, opcode);  break;
		case ARM_CODATAOP:    op_arm_codataop(cpu, opcode);    break;
		default:              op_arm_undefined(cpu, opcode);   break;
	}

	printf("cpu arm: %08x: %08x\n", cpu->pc - 4, opcode);
}

void cpu_exec_thumb(CPU *cpu) {
	uint16_t opcode = bus_read(cpu->bus, cpu->pc);
	cpu->pc += 2;
	printf("cpu thumb: %08x: %04x\n", cpu->pc - 4, opcode);
	lwarn("cpu: hit unimplemented opcode: 0x%04x\n", opcode);
}

void cpu_step(CPU *cpu) {
	// 1 cycle per instruction
	bus_clock(cpu->bus);
	if (!cpu->t) {
		cpu_exec_arm(cpu);
	} else {
		cpu_exec_thumb(cpu);
	}
}

void cpu_init(CPU *cpu) {
	init_opcode_lut();
	cpu_reset(cpu);
}

void cpu_reset(CPU *cpu) {
	cpu->pc = 0x00000000;
	cpu->t = 0;
}

void cpu_run(CPU *cpu) {
	// run until vblank
	do {
		cpu_step(cpu);
	} while (!cpu->bus->vblank_started);

	cpu->bus->vblank_started = false;
}
