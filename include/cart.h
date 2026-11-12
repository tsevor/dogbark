#pragma once

#include <stdint.h>
#include <stdbool.h>


typedef struct Cart {

	char *filename;
	char *savefile;

	uint32_t rom_size;
	uint32_t sram_size;

	uint8_t *rom;
	uint8_t *sram;

} Cart;

bool cart_init(Cart *cart, char *filename);
void cart_free(Cart *cart);

uint8_t cart_read(Cart *cart, uint32_t addr);
void cart_write(Cart *cart, uint32_t addr, uint8_t val);
