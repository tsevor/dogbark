#include "cart.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "host.h"

static SaveType cart_detect_save_type(const uint8_t *rom, size_t size) {
	if (size < 16) return SAVE_NONE;
	for (size_t i = 0; i <= size - 16; i += 4) {
		if (memcmp(rom + i, "FLASH1M_V", 9) == 0) return SAVE_FLASH1M;
		if (memcmp(rom + i, "FLASH512_V", 10) == 0) return SAVE_FLASH512;
		if (memcmp(rom + i, "FLASH_V", 7) == 0) return SAVE_FLASH512;
		if (memcmp(rom + i, "EEPROM_V", 8) == 0) return SAVE_EEPROM;
		if (memcmp(rom + i, "SRAM_V", 6) == 0) return SAVE_SRAM;
		if (memcmp(rom + i, "SRAM_F_V", 8) == 0) return SAVE_SRAM;
	}
	return SAVE_NONE;
}

static uint8_t cart_flash_read(Cart *cart, uint32_t addr) {
	Flash *flash = &cart->flash;
	uint32_t flash_addr = addr & 0xffff;

	switch (flash->read_state) {
	case FLASH_READ_ARRAY: {
		uint32_t real_addr = ((uint32_t)flash->bank << 16) | flash_addr;
		if (real_addr < cart->save_size) {
			return cart->save_data[real_addr];
		}
		return 0xff;
	}
	case FLASH_AUTOSELECT:
		if ((flash_addr & 0xff) == 0) return flash->manufacturer_id;
		if ((flash_addr & 0xff) == 1) return flash->device_id;
		return 0xff;
	case FLASH_ERASE_COMPLETE:
		flash->state = FLASH_READ_ARRAY;
		flash->read_state = FLASH_READ_ARRAY;
		return 0xff;
	default:
		return 0xff;
	}
}

static void cart_flash_write(Cart *cart, uint32_t addr, uint8_t val) {
	Flash *flash = &cart->flash;
	uint32_t flash_addr = addr & 0xffff;

	switch (flash->state) {
	case FLASH_READ_ARRAY:
		if (flash_addr == 0x5555 && val == 0xaa) {
			flash->state = FLASH_CMD_1;
		}
		break;
	case FLASH_CMD_1:
		if (flash_addr == 0x2aaa && val == 0x55) {
			flash->state = FLASH_CMD_2;
		} else {
			flash->state = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_CMD_2:
		if (flash_addr == 0x5555) {
			if (val == 0x90) {
				flash->state = FLASH_AUTOSELECT;
				flash->read_state = FLASH_AUTOSELECT;
			} else if (val == 0x80) {
				flash->state = FLASH_CMD_3;
			} else if (val == 0xf0) {
				flash->state = FLASH_READ_ARRAY;
				flash->read_state = FLASH_READ_ARRAY;
			} else if (val == 0xa0) {
				flash->state = FLASH_PROGRAM;
			} else if (val == 0xb0 && cart->save_type == SAVE_FLASH1M) {
				flash->state = FLASH_SETBANK;
			} else {
				flash->state = FLASH_READ_ARRAY;
				flash->read_state = FLASH_READ_ARRAY;
			}
		} else {
			flash->state = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_CMD_3:
		if (flash_addr == 0x5555 && val == 0xaa) {
			flash->state = FLASH_CMD_4;
		} else {
			flash->state = FLASH_READ_ARRAY;
			flash->read_state = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_CMD_4:
		if (flash_addr == 0x2aaa && val == 0x55) {
			flash->state = FLASH_CMD_5;
		} else {
			flash->state = FLASH_READ_ARRAY;
			flash->read_state = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_CMD_5:
		if (val == 0x10) {
			memset(cart->save_data, 0xff, cart->save_size);
			cart->save_dirty = true;
			flash->state = FLASH_READ_ARRAY;
			flash->read_state = FLASH_ERASE_COMPLETE;
		} else if (val == 0x30) {
			uint32_t sector_addr = ((uint32_t)flash->bank << 16) | (flash_addr & 0xf000);
			if (sector_addr + 0x1000 <= cart->save_size) {
				memset(cart->save_data + sector_addr, 0xf, 0x1000);
				cart->save_dirty = true;
			}
			flash->state = FLASH_READ_ARRAY;
			flash->read_state = FLASH_ERASE_COMPLETE;
		} else {
			flash->state = FLASH_READ_ARRAY;
			flash->read_state = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_AUTOSELECT:
		if (val == 0xf0) {
			flash->state = FLASH_READ_ARRAY;
			flash->read_state = FLASH_READ_ARRAY;
		} else if (flash_addr == 0x5555 && val == 0xaa) {
			flash->state = FLASH_CMD_1;
		} else {
			flash->state = FLASH_READ_ARRAY;
			flash->read_state = FLASH_READ_ARRAY;
		}
		break;
	case FLASH_PROGRAM: {
		uint32_t prog_addr = ((uint32_t)flash->bank << 16) | flash_addr;
		if (prog_addr < cart->save_size) {
			cart->save_data[prog_addr] = val;
			cart->save_dirty = true;
		}
		flash->state = FLASH_READ_ARRAY;
		flash->read_state = FLASH_READ_ARRAY;
		break;
	}
	case FLASH_SETBANK:
		if (flash_addr == 0x0000) {
			flash->bank = val & 1;
		}
		flash->state = FLASH_READ_ARRAY;
		flash->read_state = FLASH_READ_ARRAY;
		break;
	case FLASH_ERASE_COMPLETE:
	default:
		flash->state = FLASH_READ_ARRAY;
		flash->read_state = FLASH_READ_ARRAY;
		break;
	}
}

bool cart_init(Cart *cart, char *filename) {
	FILE *file = fopen(filename, "rb");

	if (!file) {
		lerror("cart: file doesn't exist\n");
		return false;
	}

	fseek(file, 0, SEEK_END);
	cart->rom_size = ftell(file);

	ldeb1("cart: rom:  0x%x\n", (int)cart->rom_size);

	cart->rom = calloc(cart->rom_size, 1);
	fseek(file, 0, SEEK_SET);
	fread(cart->rom, 1, cart->rom_size, file);

	fclose(file);
	file = NULL;

	cart->filename = strdup(filename);

	cart->save_type = cart_detect_save_type(cart->rom, cart->rom_size);

	switch (cart->save_type) {
	case SAVE_FLASH1M:
		cart->save_size = 131072;
		cart->flash.manufacturer_id = 0x62;
		cart->flash.device_id = 0x13;
		ldeb1("cart: save type: flash 1m (0x%zx)\n", cart->save_size);
		break;
	case SAVE_FLASH512:
		cart->save_size = 65536;
		cart->flash.manufacturer_id = 0x32;
		cart->flash.device_id = 0x1b;
		ldeb1("cart: save type: flash 512k (0x%zx)\n", cart->save_size);
		break;
	case SAVE_EEPROM:
		cart->save_size = 8192;
		ldeb1("cart: save type: eeprom (0x%zx)\n", cart->save_size);
		break;
	case SAVE_SRAM:
		cart->save_size = 32768;
		ldeb1("cart: save type: sram (0x%zx)\n", cart->save_size);
		break;
	case SAVE_NONE:
	default:
		cart->save_size = 32768;
		cart->flash.manufacturer_id = 0x32;
		cart->flash.device_id = 0x1b;
		ldeb1("cart: save type: none (fallback 0x%zx)\n", cart->save_size);
		break;
	}

	cart->save_data = calloc(131072, 1);
	memset(cart->save_data, 0xff, 131072);
	cart->save_dirty = false;
	cart->dma_count = 0;

	cart->eeprom.mode = EEPROM_IDLE;
	cart->eeprom.bits = 0;
	cart->eeprom.byte = 0;
	cart->eeprom.address = 0;
	cart->eeprom.is_64k = false;

	cart->flash.state = FLASH_READ_ARRAY;
	cart->flash.read_state = FLASH_READ_ARRAY;
	cart->flash.bank = 0;

	size_t len = strlen(cart->filename);
	int dot = len;
	for (dot = len - 1; dot >= 0; dot--) {
		if (filename[dot] == '.') break;
	}
	if (dot == -1) dot = len;

	cart->savefile = malloc(dot + 5);
	memcpy(cart->savefile, cart->filename, dot);
	memcpy(cart->savefile + dot, ".sav\0", 5);

	file = fopen(cart->savefile, "rb");

	if (file) {
		linfo("cart: save file found\n");
		fseek(file, 0, SEEK_END);
		size_t file_size = ftell(file);
		fseek(file, 0, SEEK_SET);

		if (file_size > 131072) file_size = 131072;
		fread(cart->save_data, 1, file_size, file);

		if (cart->save_type == SAVE_EEPROM) {
			if (file_size == 512) {
				cart->eeprom.is_64k = false;
				cart->save_size = 512;
			} else if (file_size >= 8192) {
				cart->eeprom.is_64k = true;
				cart->save_size = 8192;
			}
		} else if (cart->save_type == SAVE_NONE) {
			if (file_size == 131072) {
				cart->save_type = SAVE_FLASH1M;
				cart->save_size = 131072;
			} else if (file_size == 65536) {
				cart->save_type = SAVE_FLASH512;
				cart->save_size = 65536;
			} else if (file_size == 32768) {
				cart->save_type = SAVE_SRAM;
				cart->save_size = 32768;
			}
		}

		fclose(file);
	}

	linfo("cart: name: %.*s\n", 12, cart->rom + 0xa0);
	return true;
}

void cart_free(Cart *cart) {
	free(cart->filename);
	free(cart->rom);
	if (cart->savefile && cart->save_size != 0 && cart->save_data) {
		if (cart->save_dirty) {
			FILE *file = fopen(cart->savefile, "wb");
			if (file) {
				size_t write_sz = cart->save_size;
				if (cart->save_type == SAVE_EEPROM && !cart->eeprom.is_64k) {
					write_sz = 512;
				}
				fwrite(cart->save_data, 1, write_sz, file);
				linfo("cart: save file written\n");
				fclose(file);
			} else {
				lerror("cart: save file failed to write\n");
			}
		}
		free(cart->savefile);
		free(cart->save_data);
	}
}

uint8_t cart_read(Cart *cart, uint32_t addr) {
	switch (cart->save_type) {
	case SAVE_SRAM:
		if (cart->save_data) {
			return cart->save_data[addr & 0x7fff];
		}
		return 0xff;
	case SAVE_FLASH512:
	case SAVE_FLASH1M:
		return cart_flash_read(cart, addr);
	case SAVE_NONE:
	case SAVE_EEPROM:
	default:
		return 0xff;
	}
}

void cart_write(Cart *cart, uint32_t addr, uint8_t val) {
	switch (cart->save_type) {
	case SAVE_SRAM:
		if (cart->save_data) {
			cart->save_data[addr & 0x7fff] = val;
			cart->save_dirty = true;
		}
		break;
	case SAVE_FLASH512:
	case SAVE_FLASH1M:
		cart_flash_write(cart, addr, val);
		break;
	case SAVE_NONE:
		if ((addr & 0xffff) == 0x5555 && val == 0xaa) {
			cart->save_type = SAVE_FLASH512;
			cart->save_size = 65536;
			cart_flash_write(cart, addr, val);
		} else {
			cart->save_type = SAVE_SRAM;
			cart->save_size = 32768;
			if (cart->save_data) {
				cart->save_data[addr & 0x7fff] = val;
				cart->save_dirty = true;
			}
		}
		break;
	case SAVE_EEPROM:
	default:
		break;
	}
}

uint8_t cart_eeprom_read(Cart *cart) {
	EEPROM *eeprom = &cart->eeprom;
	switch (eeprom->mode) {
	case EEPROM_IDLE:
	case EEPROM_READADDRESS:
	case EEPROM_WRITEDATA:
		return 1;
	case EEPROM_READDATA:
		eeprom->bits++;
		if (eeprom->bits == 4) {
			eeprom->mode = EEPROM_READDATA2;
			eeprom->bits = 0;
			eeprom->byte = 0;
		}
		return 0;
	case EEPROM_READDATA2: {
		int address = ((int)eeprom->address << 3);
		int mask = 1 << (7 - (eeprom->bits & 7));
		int index = (address + eeprom->byte) & (8192 - 1);
		int data = (cart->save_data[index] & mask) ? 1 : 0;
		eeprom->bits++;
		if ((eeprom->bits & 7) == 0) {
			eeprom->byte++;
		}
		if (eeprom->bits == 0x40) {
			eeprom->mode = EEPROM_IDLE;
		}
		return (uint8_t)data;
	}
	default:
		return 1;
	}
}

void cart_eeprom_write(Cart *cart, uint8_t val) {
	EEPROM *eeprom = &cart->eeprom;
	int bit = val & 1;

	switch (eeprom->mode) {
	case EEPROM_IDLE:
		eeprom->byte = 0;
		eeprom->bits = 1;
		eeprom->buffer[0] = (uint8_t)bit;
		eeprom->mode = EEPROM_READADDRESS;
		break;
	case EEPROM_READADDRESS:
		eeprom->buffer[eeprom->byte] = (eeprom->buffer[eeprom->byte] << 1) | bit;
		eeprom->bits++;
		if ((eeprom->bits & 7) == 0) {
			eeprom->byte++;
		}

		int count = cart->dma_count;
		if (count == 0x11 || count == 0x51 || (count == 0 && eeprom->is_64k)) {
			if (eeprom->bits == 0x11) {
				eeprom->is_64k = true;
				eeprom->address = (((uint16_t)(eeprom->buffer[0] & 0x3f)) << 8) | eeprom->buffer[1];
				eeprom->address &= 0x3ff;
				if (!(eeprom->buffer[0] & 0x40)) {
					eeprom->buffer[0] = (uint8_t)bit;
					eeprom->bits = 1;
					eeprom->byte = 0;
					eeprom->mode = EEPROM_WRITEDATA;
				} else {
					eeprom->mode = EEPROM_READDATA;
					eeprom->byte = 0;
					eeprom->bits = 0;
				}
			}
		} else {
			if (eeprom->bits == 9) {
				eeprom->address = (eeprom->buffer[0] & 0x3f);
				if (!(eeprom->buffer[0] & 0x40)) {
					eeprom->buffer[0] = (uint8_t)bit;
					eeprom->bits = 1;
					eeprom->byte = 0;
					eeprom->mode = EEPROM_WRITEDATA;
				} else {
					eeprom->mode = EEPROM_READDATA;
					eeprom->byte = 0;
					eeprom->bits = 0;
				}
			}
		}
		break;
	case EEPROM_READDATA:
	case EEPROM_READDATA2:
		eeprom->mode = EEPROM_IDLE;
		break;
	case EEPROM_WRITEDATA:
		eeprom->buffer[eeprom->byte] = (eeprom->buffer[eeprom->byte] << 1) | bit;
		eeprom->bits++;
		if ((eeprom->bits & 7) == 0) {
			eeprom->byte++;
		}
		if (eeprom->bits == 0x40) {
			int base = ((int)eeprom->address << 3) & (8192 - 8);
			for (int i = 0; i < 8; i++) {
				cart->save_data[base + i] = eeprom->buffer[i];
			}
			cart->save_dirty = true;
		} else if (eeprom->bits == 0x41) {
			eeprom->mode = EEPROM_IDLE;
			eeprom->byte = 0;
			eeprom->bits = 0;
		}
		break;
	}
}
