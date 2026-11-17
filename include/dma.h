#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct Bus Bus;

typedef struct DMAChannel {
	uint32_t sad;
	uint32_t dad;
	uint16_t count;
	uint16_t cnt_h;

	uint32_t current_sad;
	uint32_t current_dad;
	uint32_t current_count;

	bool active;
} DMAChannel;

typedef struct DMA {
	DMAChannel chan[4];
	Bus *bus;
} DMA;

void dma_init(DMA *dma, Bus *bus);
void dma_reset(DMA *dma);
void dma_clock(DMA *dma);

void dma_check_vblank(DMA *dma);
void dma_check_hblank(DMA *dma);

uint8_t dma_read8(DMA *dma, uint8_t addr);
uint16_t dma_read16(DMA *dma, uint8_t addr);
uint32_t dma_read32(DMA *dma, uint8_t addr);

void dma_write8(DMA *dma, uint8_t addr, uint8_t val);
void dma_write16(DMA *dma, uint8_t addr, uint16_t val);
void dma_write32(DMA *dma, uint8_t addr, uint32_t val);
