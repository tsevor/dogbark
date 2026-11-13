#include "cart.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "host.h"

#define _DONT_SAVE_SRAM_FILE

bool cart_init(Cart *cart, char *filename) {

	FILE *file = fopen(filename, "rb");

	if (!file) {
		lerror("cart: file doesn't exist\n");
		return false;
	}

	fseek(file, 0, SEEK_END);
	cart->rom_size = ftell(file);

	// load full rom
	ldeb1("cart: rom:  0x%x\n", (int)cart->rom_size);

	cart->rom = calloc(cart->rom_size, 1);
	fseek(file, 0, SEEK_SET);
	fread(cart->rom, 1, cart->rom_size, file);

	fclose(file);
	file = NULL;

	cart->filename = strdup(filename);

	// no easy way to detect, assume max
	cart->sram_size = 32 << 10; // 32kb
	cart->sram = calloc(cart->sram_size, 1);
	ldeb1("cart: sram: 0x%x\n", cart->sram_size);

	size_t len = strlen(cart->filename);
	int dot = len;
	for (dot = len - 1; dot >= 0; dot--) {
		if (filename[dot] == '.') break;
	}
	if (dot == -1) dot = len; // add to end
	cart->savefile = malloc(dot + 5);
	memcpy(cart->savefile, cart->filename, dot);
	memcpy(cart->savefile + dot, ".srm\0", 5);

	file = fopen(cart->savefile, "rb");
	if (file) {
		linfo("cart: save file found\n");
		fseek(file, 0, SEEK_END);
		size_t file_size = ftell(file);
		if (file_size != cart->sram_size)
			lerror("cart: save file is wrong size\n");
		else {
			fseek(file, 0, SEEK_SET);
			fread(cart->sram, 1, cart->sram_size, file);
		}
	}

	linfo("cart: name: %.*s\n", 12, cart->rom + 0xa0);

	return true;
}

void cart_free(Cart *cart) {
	free(cart->filename);
	free(cart->rom);
	if (cart->sram_size != 0) {
#ifndef _DONT_SAVE_SRAM_FILE // less clutter during development
		FILE *file = fopen(cart->savefile, "wb");
		if (file) {
			fwrite(cart->sram, 1, cart->sram_size, file);
			linfo("cart: save file written\n");
			fclose(file);
		} else {
			lerror("cart: save file failed to write\n");
		}
#endif
		free(cart->savefile);
		free(cart->sram);
	}
}

uint32_t cart_read(Cart *cart, uint32_t addr) {
	uint8_t wait_state = (addr & 0x06000000) >> 25;
	addr = addr & 0x00ffffff;
	if (wait_state < 3) { // rom
		if (addr < cart->rom_size) {
			return *(uint32_t*)(cart->rom + addr);
		}
	} else { // sram
		if (addr < cart->sram_size) {
			return cart->sram[addr];
		}
	}
	return 0;
}

void cart_write(Cart *cart, uint32_t addr, uint8_t val) {
	uint8_t wait_state = (addr & 0x06000000) >> 25;
	addr = addr & 0x00ffffff;
	if (wait_state < 3) { // rom
		return;
	} else { // sram
		if (addr < cart->sram_size) {
			cart->sram[addr] = val;
		}
	}
}
