#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
	SAVE_NONE,
	SAVE_SRAM,
	SAVE_EEPROM,
	SAVE_FLASH512,
	SAVE_FLASH1M,
} SaveType;

typedef enum {
	EEPROM_IDLE,
	EEPROM_READADDRESS,
	EEPROM_READDATA,
	EEPROM_READDATA2,
	EEPROM_WRITEDATA,
} EEPROMMode;

typedef struct {
	EEPROMMode mode;
	int bits;
	int byte;
	uint16_t address;
	uint8_t buffer[16];
	bool is_64k;
} EEPROM;

typedef enum {
	FLASH_READ_ARRAY,
	FLASH_CMD_1,
	FLASH_CMD_2,
	FLASH_AUTOSELECT,
	FLASH_CMD_3,
	FLASH_CMD_4,
	FLASH_CMD_5,
	FLASH_ERASE_COMPLETE,
	FLASH_PROGRAM,
	FLASH_SETBANK,
} FlashState;

typedef struct {
	FlashState state;
	FlashState read_state;
	int bank;
	uint8_t manufacturer_id;
	uint8_t device_id;
} Flash;

typedef struct Cart {
	char *filename;
	char *savefile;

	uint32_t rom_size;
	uint8_t *rom;

	SaveType save_type;
	uint8_t *save_data;
	size_t save_size;
	bool save_dirty;

	EEPROM eeprom;
	Flash flash;
	int dma_count;
} Cart;

bool cart_init(Cart *cart, char *filename);
void cart_free(Cart *cart);

uint8_t cart_read(Cart *cart, uint32_t addr);
void cart_write(Cart *cart, uint32_t addr, uint8_t val);

uint8_t cart_eeprom_read(Cart *cart);
void cart_eeprom_write(Cart *cart, uint8_t val);
