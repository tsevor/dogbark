#include <stdio.h>
#include <stdlib.h>

#include "bus.h"
#include "cart.h"
#include "cpu.h"
#include "host.h"

int loglevel = 3;

int main(int argc, char **argv) {

	if (argc < 2) {
		fprintf(stderr, "usage: %s <rom.gba>\n", argv[0]);
		return 1;
	}

	Cart cart = {0};

	if (!cart_init(&cart, argv[1])) {
		fprintf(stderr, "failed to load cart: %s\n", argv[1]);
		return 1;
	}

	int hecode = host_init();

	if (hecode) {
		fprintf(stderr, "failed to init host interface: %d\n", hecode);
		return 1;
	}

	// declare components
	Bus bus = { .cart = &cart };
	CPU cpu = { .bus  = &bus  };

	bus.cpu = &cpu;

	// init components

	bus_init(&bus);

	// read in bios

	FILE *file = fopen("gba_bios.bin", "rb");

	if (!file) {
		fprintf(stderr, "\"gba_bios.bin\" doesn't exist in working directory\n");
		return 1;
	}

	fseek(file, 0, SEEK_END);
	size_t fsize = ftell(file);

	if (fsize != 16384) {
		fprintf(stderr, "\"gba_bios.bin\" is the wrong size\n");
		return 1;
	}

	bus.bios = malloc(fsize);
	fseek(file, 0, SEEK_SET);
	fread(bus.bios, 1, fsize, file);

	fclose(file);
	file = NULL;

	while (1) {
		// vblank
		cpu_run(&cpu);
		host_frame();

		EmuCtlEvent e;
		while (host_get_event(&e)) {
			switch (e.type) {
			case E_EXIT:
				bus_free(&bus);
				return 0;
			case E_RESET:
				bus_reset(&bus);
				host_send_info(I_INFO, "reset system");
				break;
			default:
				break;
			}
		}
	}
}
