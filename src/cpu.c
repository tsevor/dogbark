#include "cpu.h"
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

OpClass classify_arm_opcode(uint32_t opcode) {
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
				else if ((opcode & 0x0f900000) == 0x01000000) return ARM_PSR_REG;
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

void init_arm_opcode_lut() {
	for (int i = 0; i < 4096; i++) {
		uint32_t op = ((i & 0xff0) << 16) | ((i & 0xf) << 4);
		OPCODE_CLASS[i] = classify_arm_opcode(op);
	}
}

static const uint16_t ARM_COND[16] = {
	0x56aa, 0x6a6a, 0x55a6, 0x6966,
	0x66a9, 0x6a69, 0x66a5, 0x6a65,
	0x6a9a, 0x565a, 0x6996, 0x5556,
	0x6a99, 0x6659, 0x6a95, 0x6655,
};

static inline bool eval_op_arm_cond(CPU *cpu, uint32_t opcode) {
	return (ARM_COND[cpu->cpsr >> 28] >> (opcode >> 28)) & 1;
}

static inline void set_nz_flags(CPU *cpu, uint32_t val) {
	cpu->n = ((val & 0x80000000) != 0);
	cpu->z = (val == 0);
}

static inline int get_mode_idx(OperatingMode mode) {
	switch (mode) {
		case OPMODE_FIQ: return 1;
		case OPMODE_IRQ: return 2;
		case OPMODE_SWI: return 3;
		case OPMODE_ABT: return 4;
		case OPMODE_UND: return 5;
		default: return 0; // user and sys share banking
	}
}

void swap_banked_registers(CPU *cpu, OperatingMode old_mode, OperatingMode new_mode) {
	int old_idx = get_mode_idx(old_mode);
	int new_idx = get_mode_idx(new_mode);

	if (old_idx == new_idx) return;

	cpu->banked_r13[old_idx] = cpu->sp;
	cpu->banked_r14[old_idx] = cpu->lr;
	cpu->banked_spsr[old_idx] = cpu->spsr;

	if (old_idx == 1) {
		for (int i = 0; i < 5; i++) cpu->fiq_r8_12[i] = cpu->regs[8 + i];
	} else if (new_idx == 1) {
		for (int i = 0; i < 5; i++) cpu->usr_r8_12[i] = cpu->regs[8 + i];
	}

	cpu->regs[13] = cpu->banked_r13[new_idx];
	cpu->lr = cpu->banked_r14[new_idx];
	cpu->spsr = cpu->banked_spsr[new_idx];

	if (new_idx == 1) {
		for (int i = 0; i < 5; i++) cpu->regs[8 + i] = cpu->fiq_r8_12[i];
	} else if (old_idx == 1) {
		for (int i = 0; i < 5; i++) cpu->regs[8 + i] = cpu->usr_r8_12[i];
	}
}

static inline uint32_t ror(uint32_t val, uint32_t shift) {
	shift &= 31;
	if (shift == 0) return val;
	return (val >> shift) | (val << (32 - shift));
}

static uint32_t eval_op2(CPU *cpu, uint32_t opcode, bool *carry) {
	if (opcode & (1 << 25)) {
		uint32_t imm = opcode & 0xff;
		uint32_t rotate = ((opcode >> 8) & 0xf) * 2;
		if (rotate == 0) {
			return imm;
		}
		uint32_t res = ror(imm, rotate);
		*carry = (res >> 31) & 1;
		return res;
	}

	uint8_t rm = opcode & 0xf;
	uint32_t val = cpu->regs[rm];
	uint8_t type = (opcode >> 5) & 0x3;
	uint32_t shift;

	if ((opcode & (1 << 4)) == 0) {
		shift = (opcode >> 7) & 0x1f;

		switch (type) {
			case 0: // lsl
				if (shift == 0) return val;
				*carry = (val >> (32 - shift)) & 1;
				return val << shift;

			case 1: // lsr
				if (shift == 0) {
					*carry = (val >> 31) & 1;
					return 0;
				}
				*carry = (val >> (shift - 1)) & 1;
				return val >> shift;

			case 2: // asr
				if (shift == 0) {
					*carry = (val >> 31) & 1;
					return ((int32_t)val < 0) ? 0xffffffff : 0;
				}
				*carry = (val >> (shift - 1)) & 1;
				return (uint32_t)((int32_t)val >> shift);

			case 3: // ror / rrx
				if (shift == 0) {
					bool old_carry = *carry;
					*carry = val & 1;
					return (val >> 1) | ((uint32_t)old_carry << 31);
				}
				*carry = (val >> (shift - 1)) & 1;
				return ror(val, shift);
		}
	} else {
		uint8_t rs = (opcode >> 8) & 0xf;
		shift = cpu->regs[rs] & 0xff;

		if (shift == 0) {
			return val;
		}

		switch (type) {
			case 0: // lsl
				if (shift < 32) {
					*carry = (val >> (32 - shift)) & 1;
					return val << shift;
				} else if (shift == 32) {
					*carry = val & 1;
					return 0;
				} else {
					*carry = 0;
					return 0;
				}

			case 1: // lsr
				if (shift < 32) {
					*carry = (val >> (shift - 1)) & 1;
					return val >> shift;
				} else if (shift == 32) {
					*carry = (val >> 31) & 1;
					return 0;
				} else {
					*carry = 0;
					return 0;
				}

			case 2: // asr
				if (shift < 32) {
					*carry = (val >> (shift - 1)) & 1;
					return (uint32_t)((int32_t)val >> shift);
				} else {
					bool sign = (val >> 31) & 1;
					*carry = sign;
					return sign ? 0xffffffff : 0;
				}

			case 3: // ror
				shift &= 31;
				if (shift == 0) {
					*carry = (val >> 31) & 1;
					return val;
				}
				*carry = (val >> (shift - 1)) & 1;
				return ror(val, shift);
		}
	}

	return 0; // shut up compiler
}

void op_dp_cmp(CPU *cpu, uint32_t op1, uint32_t op2) {
	uint32_t res = op1 - op2;
	set_nz_flags(cpu, res);
	cpu->c = (op1 >= op2);
	cpu->v = (((op1 ^ op2) & (op1 ^ res)) & 0x80000000) != 0;
}

void op_dp_cmn(CPU *cpu, uint32_t op1, uint32_t op2) {
	uint64_t res64 = (uint64_t)op1 + (uint64_t)op2;
	uint32_t res = (uint32_t)res64;
	set_nz_flags(cpu, res);
	cpu->c = (res64 >> 32) & 1;
	cpu->v = (~(op1 ^ op2) & (op1 ^ res)) & 0x80000000;
}

void op_dp_teq(CPU *cpu, uint32_t op1, uint32_t op2, bool shifter_carry) {
	uint32_t res = op1 ^ op2;
	cpu->n = (res >> 31) & 1;
	cpu->z = (res == 0);
	cpu->c = shifter_carry;
}

void op_dp_tst(CPU *cpu, uint32_t op1, uint32_t op2, bool carry) {
	uint32_t res = op1 & op2;
	cpu->n = (res >> 31) & 1;
	cpu->z = (res == 0);
	cpu->c = carry;
}

static inline void dp_write_logical_res(CPU *cpu, uint8_t rd, uint32_t res, bool s, bool carry) {
	cpu->regs[rd] = res;

	if (rd == 15) {
		if (s) {
			OperatingMode old_mode = cpu->mode;
			cpu->cpsr = cpu->spsr;
			if (old_mode != cpu->mode) {
				swap_banked_registers(cpu, old_mode, cpu->mode);
			}
		}
		cpu->pc += (cpu->t ? 2 : 4);
	} else if (s) {
		set_nz_flags(cpu, res);
		cpu->c = carry;
	}
}

void op_dp_and(CPU *cpu, uint8_t rd, uint32_t op1, uint32_t op2, bool s, bool carry) {
	dp_write_logical_res(cpu, rd, op1 & op2, s, carry);
}

void op_dp_eor(CPU *cpu, uint8_t rd, uint32_t op1, uint32_t op2, bool s, bool carry) {
	dp_write_logical_res(cpu, rd, op1 ^ op2, s, carry);
}

void op_dp_orr(CPU *cpu, uint8_t rd, uint32_t op1, uint32_t op2, bool s, bool carry) {
	dp_write_logical_res(cpu, rd, op1 | op2, s, carry);
}

void op_dp_bic(CPU *cpu, uint8_t rd, uint32_t op1, uint32_t op2, bool s, bool carry) {
	dp_write_logical_res(cpu, rd, op1 & ~op2, s, carry);
}

static inline void dp_write_arithmetic_res(CPU *cpu, uint8_t rd, uint32_t res, bool s, bool carry, bool overflow) {
	cpu->regs[rd] = res;

	if (rd == 15) {
		if (s) {
			OperatingMode old_mode = cpu->mode;
			cpu->cpsr = cpu->spsr;
			if (old_mode != cpu->mode) {
				swap_banked_registers(cpu, old_mode, cpu->mode);
			}
		}
		cpu->pc += (cpu->t ? 2 : 4);
	} else if (s) {
		set_nz_flags(cpu, res);
		cpu->c = carry;
		cpu->v = overflow;
	}
}

void op_dp_add(CPU *cpu, uint8_t rd, uint32_t op1, uint32_t op2, bool s) {
	uint64_t res64 = (uint64_t)op1 + (uint64_t)op2;
	uint32_t res = (uint32_t)res64;
	bool c = (res64 >> 32) & 1;
	bool v = (~(op1 ^ op2) & (op1 ^ res)) & 0x80000000;
	dp_write_arithmetic_res(cpu, rd, res, s, c, v);
}

void op_dp_adc(CPU *cpu, uint8_t rd, uint32_t op1, uint32_t op2, bool s) {
	uint64_t res64 = (uint64_t)op1 + (uint64_t)op2 + (cpu->c ? 1 : 0);
	uint32_t res = (uint32_t)res64;
	bool c = (res64 >> 32) & 1;
	bool v = (~(op1 ^ op2) & (op1 ^ res)) & 0x80000000;
	dp_write_arithmetic_res(cpu, rd, res, s, c, v);
}

void op_dp_sub(CPU *cpu, uint8_t rd, uint32_t op1, uint32_t op2, bool s) {
	uint64_t res64 = (uint64_t)op1 + (uint64_t)(~op2) + 1;
	uint32_t res = (uint32_t)res64;
	bool c = (res64 >> 32) & 1;
	bool v = ((op1 ^ op2) & (op1 ^ res)) & 0x80000000;
	dp_write_arithmetic_res(cpu, rd, res, s, c, v);
}

void op_dp_sbc(CPU *cpu, uint8_t rd, uint32_t op1, uint32_t op2, bool s) {
	uint64_t res64 = (uint64_t)op1 + (uint64_t)(~op2) + (cpu->c ? 1 : 0);
	uint32_t res = (uint32_t)res64;
	bool c = (res64 >> 32) & 1;
	bool v = ((op1 ^ op2) & (op1 ^ res)) & 0x80000000;
	dp_write_arithmetic_res(cpu, rd, res, s, c, v);
}

void op_dp_rsb(CPU *cpu, uint8_t rd, uint32_t op1, uint32_t op2, bool s) {
	uint64_t res64 = (uint64_t)op2 + (uint64_t)(~op1) + 1;
	uint32_t res = (uint32_t)res64;
	bool c = (res64 >> 32) & 1;
	bool v = ((op2 ^ op1) & (op2 ^ res)) & 0x80000000;
	dp_write_arithmetic_res(cpu, rd, res, s, c, v);
}

void op_dp_rsc(CPU *cpu, uint8_t rd, uint32_t op1, uint32_t op2, bool s) {
	uint64_t res64 = (uint64_t)op2 + (uint64_t)(~op1) + (cpu->c ? 1 : 0);
	uint32_t res = (uint32_t)res64;
	bool c = (res64 >> 32) & 1;
	bool v = ((op2 ^ op1) & (op2 ^ res)) & 0x80000000;
	dp_write_arithmetic_res(cpu, rd, res, s, c, v);
}

void op_dp_mov(CPU *cpu, uint8_t rd, uint32_t op2, bool s, bool carry) {
	dp_write_logical_res(cpu, rd, op2, s, carry);
}

void op_dp_mvn(CPU *cpu, uint8_t rd, uint32_t op2, bool s, bool carry) {
	dp_write_logical_res(cpu, rd, ~op2, s, carry);
}

void op_arm_dataproc(CPU *cpu, uint32_t opcode) {
	uint8_t dpcode = (opcode >> 21) & 0xf;
	uint8_t rn = (opcode >> 16) & 0xf;
	uint8_t rd = (opcode >> 12) & 0xf;
	bool s = (opcode >> 20) & 1;

	uint32_t op1 = cpu->regs[rn];

	bool carry = cpu->c;
	uint32_t op2 = eval_op2(cpu, opcode, &carry);

	switch (dpcode) {
		case 0x0: op_dp_and(cpu, rd, op1, op2, s, carry); break;
		case 0x1: op_dp_eor(cpu, rd, op1, op2, s, carry); break;
		case 0x2: op_dp_sub(cpu, rd, op1, op2, s);        break;
		case 0x3: op_dp_rsb(cpu, rd, op1, op2, s);        break;
		case 0x4: op_dp_add(cpu, rd, op1, op2, s);        break;
		case 0x5: op_dp_adc(cpu, rd, op1, op2, s);        break;
		case 0x6: op_dp_sbc(cpu, rd, op1, op2, s);        break;
		case 0x7: op_dp_rsc(cpu, rd, op1, op2, s);        break;
		case 0x8: op_dp_tst(cpu, op1, op2, carry);        break;
		case 0x9: op_dp_teq(cpu, op1, op2, carry);        break;
		case 0xa: op_dp_cmp(cpu, op1, op2);               break;
		case 0xb: op_dp_cmn(cpu, op1, op2);               break;
		case 0xc: op_dp_orr(cpu, rd, op1, op2, s, carry); break;
		case 0xd: op_dp_mov(cpu, rd, op2, s, carry);      break;
		case 0xe: op_dp_bic(cpu, rd, op1, op2, s, carry); break;
		case 0xf: op_dp_mvn(cpu, rd, op2, s, carry);      break;
	}
}

static inline uint32_t cpu_ldr32(CPU *cpu, uint32_t addr) {
	uint32_t val = bus_read32(cpu->bus, addr);
	uint32_t roll = (addr & 3) << 3;
	return roll ? ((val >> roll) | (val << (32 - roll))) : val;
}

void op_arm_transswp12(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
	lerror("op_arm_transswp12 is unimplemented\n");
	exit(1);
}

void op_arm_mullong(CPU *cpu, uint32_t opcode) {
	bool u = (opcode >> 22) & 1;
	bool a = (opcode >> 21) & 1;
	bool s = (opcode >> 20) & 1;

	uint8_t rdhi = (opcode >> 16) & 0xf;
	uint8_t rdlo = (opcode >> 12) & 0xf;
	uint8_t rs = (opcode >> 8) & 0xf;
	uint8_t rm = opcode & 0xf;

	uint64_t res;

	if (!u) {
		// unsigned: umull / umlal
		res = (uint64_t)cpu->regs[rm] * (uint64_t)cpu->regs[rs];
		if (a) {
			uint64_t acc = ((uint64_t)cpu->regs[rdhi] << 32) | (uint64_t)cpu->regs[rdlo];
			res += acc;
		}
	} else {
		// signed: smull / smlal
		int64_t sres = (int64_t)(int32_t)cpu->regs[rm] * (int64_t)(int32_t)cpu->regs[rs];
		if (a) {
			int64_t acc = (int64_t)(((uint64_t)cpu->regs[rdhi] << 32) | (uint64_t)cpu->regs[rdlo]);
			sres += acc;
		}
		res = (uint64_t)sres;
	}

	cpu->regs[rdlo] = (uint32_t)res;
	cpu->regs[rdhi] = (uint32_t)(res >> 32);

	if (s) {
		cpu->n = (res >> 63) & 1;
		cpu->z = (res == 0);
	}
}

void op_arm_multiply(CPU *cpu, uint32_t opcode) {
	bool a = (opcode >> 21) & 1;
	bool s = (opcode >> 20) & 1;

	uint8_t rd = (opcode >> 16) & 0xf;
	uint8_t rn = (opcode >> 12) & 0xf;
	uint8_t rs = (opcode >> 8) & 0xf;
	uint8_t rm = opcode & 0xf;

	uint32_t res = cpu->regs[rm] * cpu->regs[rs];
	if (a) {
		res += cpu->regs[rn];
	}

	cpu->regs[rd] = res;

	if (s) {
		set_nz_flags(cpu, res);
	}
}

void op_arm_transimm10(CPU *cpu, uint32_t opcode) {
	bool p = (opcode >> 24) & 1; // 0 = post-indexed    ; 1 = pre-indexed
	bool u = (opcode >> 23) & 1; // 0 = subtract offset ; 1 = add offset
	bool w = (opcode >> 21) & 1; // 0 = no writeback    ; 1 = writeback
	bool l = (opcode >> 20) & 1; // 0 = store           ; 1 = load

	uint8_t rn = (opcode >> 16) & 0xf;
	uint8_t rd = (opcode >> 12) & 0xf;

	uint8_t offset_high = (opcode >> 8) & 0xf;
	uint8_t offset_low = opcode & 0xf;
	uint32_t offset = (offset_high << 4) | offset_low;

	uint8_t sh = (opcode >> 5) & 3;

	uint32_t base = cpu->regs[rn];
	int32_t effective_offset = u ? (int32_t)offset : -(int32_t)offset;

	uint32_t addr = p ? (base + effective_offset) : base;

	if (l) {
		uint32_t data = 0;

		switch (sh) {
			case 1:
				data = bus_read16(cpu->bus, addr);
				if (addr & 1) {
					data = (data >> 8) | (data << 24);
				}
				break;
			case 2:
				data = (int32_t)(int8_t)bus_read8(cpu->bus, addr);
				break;
			case 3:
				if (addr & 1) {
					data = (int32_t)(int8_t)bus_read8(cpu->bus, addr);
				} else {
					data = (int32_t)(int16_t)bus_read16(cpu->bus, addr);
				}
				break;
		}

		if ((!p || w) && rn != 15) {
			if (p) {
				cpu->regs[rn] = addr;
			} else {
				cpu->regs[rn] = base + effective_offset;
			}
		}

		cpu->regs[rd] = data;

		if (rd == 15) cpu->pc += (cpu->t ? 2 : 4);
	} else {
		uint32_t data = cpu->regs[rd];

		if (rd == 15) {
			data += 4;
		}

		bus_write16(cpu->bus, addr, data & 0xffff);

		if ((!p || w) && rn != 15) {
			if (p) {
				cpu->regs[rn] = addr;
			} else {
				cpu->regs[rn] = base + effective_offset;
			}
		}
	}
}

void op_arm_transreg10(CPU *cpu, uint32_t opcode) {
	bool p = (opcode >> 24) & 1; // 0 = post-indexed    ; 1 = pre-indexed
	bool u = (opcode >> 23) & 1; // 0 = subtract offset ; 1 = add offset
	bool w = (opcode >> 21) & 1; // 0 = no writeback    ; 1 = writeback
	bool l = (opcode >> 20) & 1; // 0 = store           ; 1 = load

	uint8_t rn = (opcode >> 16) & 0xf;
	uint8_t rd = (opcode >> 12) & 0xf;
	uint8_t rm = opcode & 0xf;
	uint32_t offset = cpu->regs[rm];

	uint8_t sh = (opcode >> 5) & 3;

	uint32_t base = cpu->regs[rn];
	int32_t effective_offset = u ? (int32_t)offset : -(int32_t)offset;

	uint32_t addr = p ? (base + effective_offset) : base;

	if (l) {
		uint32_t data = 0;

		switch (sh) {
			case 1: // LDRH
				data = bus_read16(cpu->bus, addr);
				if (addr & 1) {
					data = (data >> 8) | (data << 24);
				}
				break;
			case 2: // LDRSB
				data = (int32_t)(int8_t)bus_read8(cpu->bus, addr);
				break;
			case 3: // LDRSH
				if (addr & 1) {
					data = (int32_t)(int8_t)bus_read8(cpu->bus, addr);
				} else {
					data = (int32_t)(int16_t)bus_read16(cpu->bus, addr);
				}
				break;
		}

		if ((!p || w) && rn != 15) {
			if (p) {
				cpu->regs[rn] = addr;
			} else {
				cpu->regs[rn] = base + effective_offset;
			}
		}

		cpu->regs[rd] = data;

		if (rd == 15) cpu->pc += (cpu->t ? 2 : 4);
	} else {
		uint32_t data = cpu->regs[rd];

		if (rd == 15) {
			data += 4;
		}

		bus_write16(cpu->bus, addr, data & 0xffff);

		if ((!p || w) && rn != 15) {
			if (p) {
				cpu->regs[rn] = addr;
			} else {
				cpu->regs[rn] = base + effective_offset;
			}
		}
	}
}

void op_arm_bx(CPU *cpu, uint32_t opcode) {
	uint8_t rm = opcode & 0xf;
	uint32_t target = cpu->regs[rm];

	cpu->t = target & 1;

	if (cpu->t) {
		cpu->pc = (target & ~1) + 2;
	} else {
		cpu->pc = (target & ~3) + 4;
	}
}

static inline void update_psr(CPU *cpu, uint32_t opcode, uint32_t val) {
	bool r = (opcode >> 22) & 1;
	uint32_t field_mask = (opcode >> 16) & 0xf;
	uint32_t bitmask = 0;

	if (field_mask & 1) bitmask |= 0x000000ff;
	if (field_mask & 2) bitmask |= 0x0000ff00;
	if (field_mask & 4) bitmask |= 0x00ff0000;
	if (field_mask & 8) bitmask |= 0xff000000;

	if (r) {
		cpu->spsr = (cpu->spsr & ~bitmask) | (val & bitmask);
	} else {
		if (cpu->mode == OPMODE_USER) {
			bitmask &= 0xff000000;
		}

		OperatingMode old_mode = cpu->mode;
		cpu->cpsr = (cpu->cpsr & ~bitmask) | (val & bitmask);

		if (old_mode != cpu->mode) {
			swap_banked_registers(cpu, old_mode, cpu->mode);
		}
	}
}

void op_arm_psr_reg(CPU *cpu, uint32_t opcode) {
	bool msr = (opcode >> 21) & 1;

	if (msr) {
		uint32_t val = cpu->regs[opcode & 0xf];
		update_psr(cpu, opcode, val);
	} else {
		bool r = (opcode >> 22) & 1;
		uint8_t rd = (opcode >> 12) & 0xf;
		cpu->regs[rd] = r ? cpu->spsr : cpu->cpsr;
	}
}

void op_arm_psr_imm(CPU *cpu, uint32_t opcode) {
	uint32_t imm = opcode & 0xff;
	uint32_t rotate = ((opcode >> 8) & 0xf) * 2;
	uint32_t val = ror(imm, rotate);

	update_psr(cpu, opcode, val);
}

void op_arm_transimm9(CPU *cpu, uint32_t opcode) {
	bool p = (opcode >> 24) & 1; // 0 = post-indexed    ; 1 = pre-indexed
	bool u = (opcode >> 23) & 1; // 0 = subtract offset ; 1 = add offset
	bool b = (opcode >> 22) & 1; // 0 = word            ; 1 = byte
	bool w = (opcode >> 21) & 1; // 0 = no writeback    ; 1 = writeback
	bool l = (opcode >> 20) & 1; // 0 = store           ; 1 = load

	uint8_t rn = (opcode >> 16) & 0xf;
	uint8_t rd = (opcode >> 12) & 0xf;
	uint32_t offset = opcode & 0xfff;

	uint32_t base = cpu->regs[rn];
	uint32_t addr = base;
	uint32_t effective_offset = u ? (int32_t)offset : (-(int32_t)offset);

	if (p) addr += effective_offset;

	if (l) {
		uint32_t data;
		if (b) {
			data = bus_read8(cpu->bus, addr);
		} else {
			data = cpu_ldr32(cpu, addr);
		}
		cpu->regs[rd] = data;

		if (rd == 15) {
			cpu->pc += (cpu->t ? 2 : 4);
		}
	} else {
		uint32_t data = cpu->regs[rd];

		if (rd == 15) {
			data += 4;
		}

		if (b) {
			bus_write8(cpu->bus, addr, data & 0xff);
		} else {
			bus_write32(cpu->bus, addr, data);
		}
	}
	if ((!p || w) && rn != 15 && !(l && rd == rn)) {
		cpu->regs[rn] = p ? addr : (base + effective_offset);
	}
}

void op_arm_transreg9(CPU *cpu, uint32_t opcode) {
	bool p = (opcode >> 24) & 1; // 0 = post-indexed    ; 1 = pre-indexed
	bool u = (opcode >> 23) & 1; // 0 = subtract offset ; 1 = add offset
	bool b = (opcode >> 22) & 1; // 0 = word            ; 1 = byte
	bool w = (opcode >> 21) & 1; // 0 = no writeback    ; 1 = writeback
	bool l = (opcode >> 20) & 1; // 0 = store           ; 1 = load

	uint8_t rn = (opcode >> 16) & 0xf;
	uint8_t rd = (opcode >> 12) & 0xf;

	uint8_t rm = opcode & 0xf;
	uint32_t val = cpu->regs[rm];
	uint8_t type = (opcode >> 5) & 0x3;
	uint32_t shift = (opcode >> 7) & 0x1f;
	uint32_t offset;

	switch (type) {
		case 0: // lsl
			offset = (shift == 0) ? val : (val << shift);
			break;
		case 1: // lsr
			offset = (shift == 0) ? 0 : (val >> shift);
			break;
		case 2: // asr
			offset = (shift == 0) ? (((int32_t)val < 0) ? 0xffffffff : 0) : (uint32_t)((int32_t)val >> shift);
			break;
		case 3: // ror / rrx
			offset = (shift == 0) ? ((val >> 1) | ((uint32_t)cpu->c << 31)) : ror(val, shift);
			break;
	}

	uint32_t base = cpu->regs[rn];
	uint32_t addr = base;
	uint32_t effective_offset = u ? (int32_t)offset : (-(int32_t)offset);

	if (p) addr += effective_offset;

	if (l) {
		uint32_t data;
		if (b) {
			data = bus_read8(cpu->bus, addr);
		} else {
			data = cpu_ldr32(cpu, addr);
		}
		cpu->regs[rd] = data;

		if (rd == 15) {
			cpu->pc += (cpu->t ? 2 : 4);
		}
	} else {
		uint32_t data = cpu->regs[rd];

		if (rd == 15) {
			data += 4;
		}

		if (b) {
			bus_write8(cpu->bus, addr, data & 0xff);
		} else {
			bus_write32(cpu->bus, addr, data);
		}
	}

	if ((!p || w) && rn != 15 && !(l && rd == rn)) {
		cpu->regs[rn] = p ? addr : (base + effective_offset);
	}
}

void op_arm_blocktrans(CPU *cpu, uint32_t opcode) {
	bool p = (opcode >> 24) & 1; // 0 = post ; 1 = pre
	bool u = (opcode >> 23) & 1; // 0 = down ; 1 = up
	bool s = (opcode >> 22) & 1; // 1 = s-bit special behavior
	bool w = (opcode >> 21) & 1; // 1 = writeback
	bool l = (opcode >> 20) & 1; // 0 = stm  ; 1 = ldm

	uint8_t rn = (opcode >> 16) & 0xf;
	uint16_t rlist = opcode & 0xffff;

	uint32_t base = cpu->regs[rn];
	int num_regs = 0;

	for (int i = 0; i < 16; i++) {
		if ((rlist >> i) & 1) num_regs++;
	}

	if (num_regs == 0) {
		rlist = 0x8000;
		num_regs = 16;
	}

	uint32_t start_addr;
	if (u) {
		start_addr = p ? (base + 4) : base;
	} else {
		start_addr = p ? (base - (num_regs * 4)) : (base - (num_regs * 4) + 4);
	}

	uint32_t current_addr = start_addr;

	bool force_user_bank = (s && (!l || !(rlist & 0x8000)));
	OperatingMode old_mode = cpu->mode;

	if (force_user_bank && old_mode != OPMODE_USER) {
		swap_banked_registers(cpu, old_mode, OPMODE_USER);
	}

	for (int i = 0; i < 16; i++) {
		if ((rlist >> i) & 1) {
			if (l) {
				cpu->regs[i] = bus_read32(cpu->bus, current_addr);

				if (i == 15) {
					cpu->pc += (cpu->t ? 2 : 4);
				}
			} else {
				uint32_t val = cpu->regs[i];

				if (i == 15) val += 4;

				bus_write32(cpu->bus, current_addr, val);
			}
			current_addr += 4;
		}
	}

	if (force_user_bank && old_mode != OPMODE_USER) {
		swap_banked_registers(cpu, OPMODE_USER, old_mode);
	}

	if (w) {
		if (!(l && (rlist & (1 << rn)))) {
			cpu->regs[rn] = u ? (base + (num_regs * 4)) : (base - (num_regs * 4));
		}
	}

	if (s && l && (rlist & 0x8000)) {
		OperatingMode pre_return_mode = cpu->mode;
		cpu->cpsr = cpu->spsr;

		if (pre_return_mode != cpu->mode) {
			swap_banked_registers(cpu, pre_return_mode, cpu->mode);
		}
	}

	if (l && (rlist & 0x8000)) {
		cpu->pc += (cpu->t ? 2 : 4);
	}
}

void op_arm_bbl(CPU *cpu, uint32_t opcode) {
	bool link = (opcode >> 24) & 1;
	if (link) cpu->lr = cpu->pc - 4;
	int32_t offset = ((opcode & 0x00ffffff) ^ 0x00800000) - 0x00800000;
	cpu->pc += 4 + (offset << 2);
}

void op_arm_codatatrans(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
	lerror("op_arm_codatatrans is unimplemented\n");
	exit(1);
}

void op_arm_swi(CPU *cpu, uint32_t opcode) {
	(void)opcode;
	uint32_t return_addr = cpu->pc - 4;

	OperatingMode old_mode = cpu->mode;
	swap_banked_registers(cpu, old_mode, OPMODE_SWI);
	cpu->spsr = cpu->cpsr;
	cpu->mode = OPMODE_SWI;
	cpu->i = 1;
	cpu->t = 0;

	cpu->lr = return_addr;
	cpu->pc = 0x00000008 + 4;
}

void op_arm_coregtrans(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
	lerror("op_arm_coregtrans is unimplemented\n");
	exit(1);
}

void op_arm_codataop(CPU *cpu, uint32_t opcode) {
	(void)cpu;
	(void)opcode;
	lerror("op_arm_codataop is unimplemented\n");
	exit(1);
}

void op_arm_undefined(CPU *cpu, uint32_t opcode) {
	lwarn("cpu: hit undefined opcode: 0x%08x\n", opcode);
	(void)cpu;
	exit(1);
}

void cpu_exec_arm(CPU *cpu) {
	uint32_t opcode = bus_read32(cpu->bus, cpu->pc - 4);
	cpu->pc += 4;
	if (!eval_op_arm_cond(cpu, opcode)) {
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

}

void op_thumb_shift_imm(CPU *cpu, uint16_t opcode) {
	uint8_t op = (opcode >> 11) & 3;
	uint8_t offset = (opcode >> 6) & 0x1f;
	uint8_t rs = (opcode >> 3) & 7;
	uint8_t rd = opcode & 7;

	uint32_t val = cpu->regs[rs];
	uint32_t res = val;

	switch (op) {
		case 0: // lsl
			if (offset > 0) {
				cpu->c = (val >> (32 - offset)) & 1;
				res = val << offset;
			}
			break;
		case 1: // lsr
			if (offset == 0) {
				cpu->c = (val >> 31) & 1;
				res = 0;
			} else {
				cpu->c = (val >> (offset - 1)) & 1;
				res = val >> offset;
			}
			break;
		case 2: // asr
			if (offset == 0) {
				cpu->c = (val >> 31) & 1;
				res = ((int32_t)val < 0) ? 0xffffffff : 0;
			} else {
				cpu->c = (val >> (offset - 1)) & 1;
				res = (uint32_t)((int32_t)val >> offset);
			}
			break;
	}

	cpu->regs[rd] = res;
	set_nz_flags(cpu, res);
}

void op_thumb_add_sub(CPU *cpu, uint16_t opcode) {
	bool imm = (opcode >> 10) & 1;
	bool sub = (opcode >> 9) & 1;

	uint8_t rn_imm = (opcode >> 6) & 7;
	uint8_t rs = (opcode >> 3) & 7;
	uint8_t rd = opcode & 7;

	uint32_t op1 = cpu->regs[rs];
	uint32_t op2 = imm ? rn_imm : cpu->regs[rn_imm];

	if (sub) {
		op_dp_sub(cpu, rd, op1, op2, true);
	} else {
		op_dp_add(cpu, rd, op1, op2, true);
	}
}

void op_thumb_alu_imm(CPU *cpu, uint16_t opcode) {
	uint8_t op = (opcode >> 11) & 3;
	uint8_t rd = (opcode >> 8) & 7;
	uint32_t imm = opcode & 0xff;

	uint32_t rd_val = cpu->regs[rd];

	switch (op) {
		case 0:
			op_dp_mov(cpu, rd, imm, true, cpu->c);
			break;
		case 1:
			op_dp_cmp(cpu, rd_val, imm);
			break;
		case 2:
			op_dp_add(cpu, rd, rd_val, imm, true);
			break;
		case 3:
			op_dp_sub(cpu, rd, rd_val, imm, true);
			break;
	}
}

static inline uint32_t thumb_shift(CPU *cpu, uint8_t type, uint32_t val, uint32_t shift) {
	shift &= 0xff;
	if (shift == 0) return val;
	bool carry = cpu->c;

	switch (type) {
		case 0: // lsl
			if (shift < 32) {
				carry = (val >> (32 - shift)) & 1;
				val <<= shift;
			} else if (shift == 32) {
				carry = val & 1;
				val = 0;
			} else {
				carry = 0;
				val = 0;
			}
			break;
		case 1: // lsr
			if (shift < 32) {
				carry = (val >> (shift - 1)) & 1;
				val >>= shift;
			} else if (shift == 32) {
				carry = (val >> 31) & 1;
				val = 0;
			} else {
				carry = 0;
				val = 0;
			}
			break;
		case 2: // asr
			if (shift < 32) {
				carry = (val >> (shift - 1)) & 1;
				val = (uint32_t)((int32_t)val >> shift);
			} else {
				bool sign = (val >> 31) & 1;
				carry = sign;
				val = sign ? 0xffffffff : 0;
			}
			break;
		case 3: // ror
			shift &= 31;
			if (shift == 0) {
				carry = (val >> 31) & 1;
			} else {
				carry = (val >> (shift - 1)) & 1;
				val = (val >> shift) | (val << (32 - shift));
			}
			break;
	}
	cpu->c = carry;
	return val;
}

void op_thumb_alu_reg(CPU *cpu, uint16_t opcode) {
	uint8_t op = (opcode >> 6) & 0xf;
	uint8_t rs = (opcode >> 3) & 7;
	uint8_t rd = opcode & 7;

	uint32_t val_d = cpu->regs[rd];
	uint32_t val_s = cpu->regs[rs];

	switch (op) {
		case 0x0:
			op_dp_and(cpu, rd, val_d, val_s, true, cpu->c);
			break;
		case 0x1:
			op_dp_eor(cpu, rd, val_d, val_s, true, cpu->c);
			break;
		case 0x2: {
			uint32_t res = thumb_shift(cpu, 0, val_d, val_s);
			cpu->regs[rd] = res;
			set_nz_flags(cpu, res);
			break;
		}
		case 0x3: {
			uint32_t res = thumb_shift(cpu, 1, val_d, val_s);
			cpu->regs[rd] = res;
			set_nz_flags(cpu, res);
			break;
		}
		case 0x4: {
			uint32_t res = thumb_shift(cpu, 2, val_d, val_s);
			cpu->regs[rd] = res;
			set_nz_flags(cpu, res);
			break;
		}
		case 0x5:
			op_dp_adc(cpu, rd, val_d, val_s, true);
			break;
		case 0x6:
			op_dp_sbc(cpu, rd, val_d, val_s, true);
			break;
		case 0x7: {
			uint32_t res = thumb_shift(cpu, 3, val_d, val_s);
			cpu->regs[rd] = res;
			set_nz_flags(cpu, res);
			break;
		}
		case 0x8:
			op_dp_tst(cpu, val_d, val_s, cpu->c);
			break;
		case 0x9:
			op_dp_sub(cpu, rd, 0, val_s, true);
			break;
		case 0xa:
			op_dp_cmp(cpu, val_d, val_s);
			break;
		case 0xb:
			op_dp_cmn(cpu, val_d, val_s);
			break;
		case 0xc:
			op_dp_orr(cpu, rd, val_d, val_s, true, cpu->c);
			break;
		case 0xd: {
			uint32_t res = val_d * val_s;
			cpu->regs[rd] = res;
			set_nz_flags(cpu, res);
			break;
		}
		case 0xe:
			op_dp_bic(cpu, rd, val_d, val_s, true, cpu->c);
			break;
		case 0xf:
			op_dp_mvn(cpu, rd, val_s, true, cpu->c);
			break;
	}
}

void op_thumb_hireg_bx(CPU *cpu, uint16_t opcode) {
	uint8_t op = (opcode >> 8) & 3;

	uint8_t rd = ((opcode >> 4) & 8) | (opcode & 7);
	uint8_t rs = ((opcode >> 3) & 8) | ((opcode >> 3) & 7);

	uint32_t val_s = cpu->regs[rs];
	uint32_t val_d = cpu->regs[rd];

	switch (op) {
		case 0: { // add
			uint32_t res = val_d + val_s;
			if (rd == 15) {
				cpu->pc = (res & ~1) + 2;
			} else {
				cpu->regs[rd] = res;
			}
			break;
		}
		case 1: // cmp
			op_dp_cmp(cpu, val_d, val_s);
			break;
		case 2: // mov
			if (rd == 15) {
				cpu->pc = (val_s & ~1) + 2;
			} else {
				cpu->regs[rd] = val_s;
			}
			break;
		case 3: { // bx
			cpu->t = val_s & 1;
			if (cpu->t) {
				cpu->pc = (val_s & ~1) + 2;
			} else {
				cpu->pc = (val_s & ~3) + 4;
			}
			break;
		}
	}
}

void op_thumb_ldr_pc(CPU *cpu, uint16_t opcode) {
	uint8_t rd = (opcode >> 8) & 7;

	uint32_t offset = (opcode & 0xff) << 2;

	uint32_t base = cpu->pc & ~3;
	uint32_t addr = base + offset;

	cpu->regs[rd] = bus_read32(cpu->bus, addr);
}

void op_thumb_trans_reg(CPU *cpu, uint16_t opcode) {
	uint8_t rd = opcode & 7;
	uint8_t rb = (opcode >> 3) & 7;
	uint8_t ro = (opcode >> 6) & 7;

	bool fmt8 = (opcode >> 9) & 1;

	uint32_t addr = cpu->regs[rb] + cpu->regs[ro];

	if (!fmt8) {
		bool l = (opcode >> 11) & 1;
		bool b = (opcode >> 10) & 1;
		if (l) {
			if (b) {
				cpu->regs[rd] = bus_read8(cpu->bus, addr);
			} else {
				cpu->regs[rd] = cpu_ldr32(cpu, addr);
			}
		} else {
			if (b) {
				bus_write8(cpu->bus, addr, cpu->regs[rd] & 0xff);
			} else {
				bus_write32(cpu->bus, addr, cpu->regs[rd]);
			}
		}
	} else {
		uint8_t op = (opcode >> 10) & 3;
		switch (op) {
			case 0:
				bus_write16(cpu->bus, addr, cpu->regs[rd] & 0xffff);
				break;
			case 1: {
					uint32_t data = (int32_t)(int8_t)bus_read8(cpu->bus, addr);
					cpu->regs[rd] = data;
				}
				break;
			case 2: {
				uint32_t data = bus_read16(cpu->bus, addr);
				if (addr & 1) {
					data = (data >> 8) | (data << 24);
				}
				cpu->regs[rd] = data;
			}
			break;
			case 3: {
					uint32_t data;
					if (addr & 1) {
						data = (int32_t)(int8_t)bus_read8(cpu->bus, addr);
					} else {
						data = (int32_t)(int16_t)bus_read16(cpu->bus, addr);
					}
					cpu->regs[rd] = data;
				}
				break;
		}
	}
}

void op_thumb_trans_imm(CPU *cpu, uint16_t opcode) {
	bool b = (opcode >> 12) & 1;
	bool l = (opcode >> 11) & 1;
	uint32_t offset = (opcode >> 6) & 0x1f;
	uint8_t rb = (opcode >> 3) & 7;
	uint8_t rd = opcode & 7;

	if (!b) offset <<= 2;

	uint32_t addr = cpu->regs[rb] + offset;

	if (l) {
		if (b) {
			cpu->regs[rd] = bus_read8(cpu->bus, addr);
		} else {
			cpu->regs[rd] = cpu_ldr32(cpu, addr);
		}
	} else {
		if (b) {
			bus_write8(cpu->bus, addr, cpu->regs[rd] & 0xff);
		} else {
			bus_write32(cpu->bus, addr, cpu->regs[rd]);
		}
	}
}

void op_thumb_trans_hw(CPU *cpu, uint16_t opcode) {
	bool l = (opcode >> 11) & 1;
	uint32_t offset = ((opcode >> 6) & 0x1f) << 1;
	uint8_t rb = (opcode >> 3) & 7;
	uint8_t rd = opcode & 7;

	uint32_t addr = cpu->regs[rb] + offset;

	if (l) {
		uint32_t data = bus_read16(cpu->bus, addr);
		if (addr & 1) {
			data = (data >> 8) | (data << 24);
		}
		cpu->regs[rd] = data;
	} else {
		bus_write16(cpu->bus, addr, cpu->regs[rd] & 0xffff);
	}
}

void op_thumb_trans_sp(CPU *cpu, uint16_t opcode) {
	bool l = (opcode >> 11) & 1;
	uint8_t rd = (opcode >> 8) & 7;
	uint32_t offset = (opcode & 0xff) << 2;

	uint32_t addr = cpu->sp + offset;

	if (l) {
		cpu->regs[rd] = cpu_ldr32(cpu, addr);
	} else {
		bus_write32(cpu->bus, addr, cpu->regs[rd]);
	}
}

void op_thumb_add_pc_sp(CPU *cpu, uint16_t opcode) {
	bool s = (opcode >> 11) & 1;
	uint8_t rd = (opcode >> 8) & 7;
	uint32_t imm = (opcode & 0xff) << 2;

	if (!s) {
		cpu->regs[rd] = (cpu->pc & ~2) + imm;
	} else {
		cpu->regs[rd] = cpu->sp + imm;
	}
}

void op_thumb_add_sp(CPU *cpu, uint16_t opcode) {
	bool s = (opcode >> 7) & 1;
	uint32_t imm = (opcode & 0x7f) << 2;

	if (s) {
		cpu->sp -= imm;
	} else {
		cpu->sp += imm;
	}
}

void op_thumb_push_pop(CPU *cpu, uint16_t opcode) {
	bool l = (opcode >> 11) & 1;
	bool r = (opcode >> 8) & 1;
	uint8_t rlist = opcode & 0xff;

	int count = 0;
	for (int i = 0; i < 8; i++) {
		if ((rlist >> i) & 1) count++;
	}
	if (r) count++;

	if (!l) {
		uint32_t sp = cpu->sp - (count * 4);
		uint32_t addr = sp;

		for (int i = 0; i < 8; i++) {
			if ((rlist >> i) & 1) {
				bus_write32(cpu->bus, addr, cpu->regs[i]);
				addr += 4;
			}
		}

		if (r) {
			bus_write32(cpu->bus, addr, cpu->lr);
		}

		cpu->sp = sp;
	} else {
		uint32_t addr = cpu->sp;

		for (int i = 0; i < 8; i++) {
			if ((rlist >> i) & 1) {
				cpu->regs[i] = bus_read32(cpu->bus, addr);
				addr += 4;
			}
		}

		if (r) {
			uint32_t target = bus_read32(cpu->bus, addr);
			addr += 4;
			cpu->pc = (target & ~1) + 2;
		}

		cpu->sp = addr;
	}
}

void op_thumb_blocktrans(CPU *cpu, uint16_t opcode) {
	bool l = (opcode >> 11) & 1;
	uint8_t rb = (opcode >> 8) & 7;
	uint8_t rlist = opcode & 0xff;

	uint32_t addr = cpu->regs[rb];

	for (int i = 0; i < 8; i++) {
		if ((rlist >> i) & 1) {
			if (!l) {
				bus_write32(cpu->bus, addr, cpu->regs[i]);
			} else {
				cpu->regs[i] = bus_read32(cpu->bus, addr);
			}
			addr += 4;
		}
	}

	if (!l || !((rlist >> rb) & 1)) {
		cpu->regs[rb] = addr;
	}
}

void op_thumb_swi(CPU *cpu, uint16_t opcode) {
	(void)opcode;
	uint32_t return_addr = cpu->pc - 2;

	OperatingMode old_mode = cpu->mode;
	swap_banked_registers(cpu, old_mode, OPMODE_SWI);
	cpu->spsr = cpu->cpsr;
	cpu->mode = OPMODE_SWI;
	cpu->i = 1;
	cpu->t = 0;

	cpu->lr = return_addr;
	cpu->pc = 0x00000008 + 4;
}

void op_thumb_branch_cond(CPU *cpu, uint16_t opcode) {
	uint8_t cond = (opcode >> 8) & 0xf;

	bool cond_met = (ARM_COND[cpu->cpsr >> 28] >> cond) & 1;
	if (!cond_met) return;

	int32_t offset = (int8_t)(opcode & 0xff);
	cpu->pc += 2 + (offset << 1);
}

void op_thumb_branch(CPU *cpu, uint16_t opcode) {
	int32_t offset = ((opcode & 0x7ff) ^ 0x400) - 0x400;
	cpu->pc += 2 + (offset << 1);
}

void op_thumb_branch_link(CPU *cpu, uint16_t opcode) {
	bool h = (opcode >> 11) & 1;

	if (!h) {
		int32_t offset = ((opcode & 0x7ff) ^ 0x400) - 0x400;
		cpu->lr = cpu->pc + (offset << 12);
	} else {
		uint32_t offset = (opcode & 0x7ff) << 1;
		uint32_t target = cpu->lr + offset;

		cpu->lr = (cpu->pc - 2) | 1;
		cpu->pc = (target & ~1) + 2;
	}
}

void cpu_exec_thumb(CPU *cpu) {
	uint16_t opcode = bus_read16(cpu->bus, cpu->pc - 2);
	cpu->pc += 2;

	uint8_t format = opcode >> 10;

	switch (format) {
		case 0x00 ... 0x05:
			op_thumb_shift_imm(cpu, opcode);
			break;
		case 0x06 ... 0x07:
			op_thumb_add_sub(cpu, opcode);
			break;
		case 0x08 ... 0x0f:
			op_thumb_alu_imm(cpu, opcode);
			break;
		case 0x10:
			op_thumb_alu_reg(cpu, opcode);
			break;
		case 0x11:
			op_thumb_hireg_bx(cpu, opcode);
			break;
		case 0x12 ... 0x13:
			op_thumb_ldr_pc(cpu, opcode);
			break;
		case 0x14 ... 0x17:
			op_thumb_trans_reg(cpu, opcode);
			break;
		case 0x18 ... 0x1f:
			op_thumb_trans_imm(cpu, opcode);
			break;
		case 0x20 ... 0x23:
			op_thumb_trans_hw(cpu, opcode);
			break;
		case 0x24 ... 0x27:
			op_thumb_trans_sp(cpu, opcode);
			break;
		case 0x28 ... 0x2b:
			op_thumb_add_pc_sp(cpu, opcode);
			break;
		case 0x2c ... 0x2f:
			if ((opcode & 0xff00) == 0xb000) {
				op_thumb_add_sp(cpu, opcode);
			} else if ((opcode & 0xf600) == 0xb400) {
				op_thumb_push_pop(cpu, opcode);
			} else {
				lwarn("cpu: undefined thumb opcode: 0x%04x\n", opcode);
			}
			break;
		case 0x30 ... 0x33:
			op_thumb_blocktrans(cpu, opcode);
			break;
		case 0x34 ... 0x37:
			if ((opcode & 0xff00) == 0xdf00) {
				op_thumb_swi(cpu, opcode);
			} else {
				op_thumb_branch_cond(cpu, opcode);
			}
			break;
		case 0x38 ... 0x39:
			op_thumb_branch(cpu, opcode);
			break;
		case 0x3a ... 0x3b:
			lwarn("cpu: undefined thumb opcode: 0x%04x\n", opcode);
			break;
		case 0x3c ... 0x3f:
			op_thumb_branch_link(cpu, opcode);
			break;
	}
}

void cpu_step(CPU *cpu) {
	bus_clock(cpu->bus);

	if (cpu->bus->io_ie & cpu->bus->io_if) {
		cpu->bus->halted = false;
	}

	if (cpu->bus->halted) {
		return;
	}

	if (cpu->bus->io_ime && (cpu->bus->io_ie & cpu->bus->io_if) && !cpu->i) {
		OperatingMode old_mode = cpu->mode;
		swap_banked_registers(cpu, old_mode, OPMODE_IRQ);
		cpu->spsr = cpu->cpsr;
		cpu->mode = OPMODE_IRQ;
		cpu->i = 1;

		cpu->lr = cpu->t ? (cpu->pc + 2) : cpu->pc;
		cpu->t = 0;

		cpu->pc = 0x00000018 + 4;
		return;
	}

	if (!cpu->t) {
		cpu_exec_arm(cpu);
	} else {
		cpu_exec_thumb(cpu);
	}
}

void cpu_init(CPU *cpu) {
	init_arm_opcode_lut();
	cpu_reset(cpu);
}

void cpu_reset(CPU *cpu) {
	cpu->pc = 0x00000004;
	cpu->t = 0;
}

void cpu_run(CPU *cpu) {
	// run until vblank
	do {
		cpu_step(cpu);
	} while (!cpu->bus->vblank_started);

	cpu->bus->vblank_started = false;
}
