#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// everything in here should be a blind interface

#define GBA_WIDTH 240
#define GBA_HEIGHT 160
#define PIXEL_SCALE 2

typedef enum ControllerButton {
	BUTTON_A      = 0x0001,
	BUTTON_B      = 0x0002,
	BUTTON_SELECT = 0x0004,
	BUTTON_START  = 0x0008,
	BUTTON_RIGHT  = 0x0010,
	BUTTON_LEFT   = 0x0020,
	BUTTON_UP     = 0x0040,
	BUTTON_DOWN   = 0x0080,
	BUTTON_R      = 0x0100,
	BUTTON_L      = 0x0200,
} ControllerButton;

typedef enum EmuCtlEventType {
	E_NONE,
	E_LOADROM,
	E_RESET,
	E_EXIT,
} EmuCtlEventType;

typedef struct EmuCtlEvent {
	EmuCtlEventType type;
	void* arg;
} EmuCtlEvent;

typedef enum InfoType {
	I_NONE,
	I_FAIL,
	I_INFO,
	I_MISC,
} InfoType;

extern uint32_t *host_screen_buffer;

// called on emulator start
int host_init();

// called on vblank for host event processing
void host_frame();

// called on emulator exit to free resources
void host_exit();

// swap internal buffer with exposed one
void host_swap_buffer();

// copy exposed buffer to internal one
void host_copy_buffer();

// queue 'len' stereo interlaced samples (l, r, l, r...) to play
void host_queue_audio_buffer(int16_t *buffer, size_t len);

// clears pending audio buffers
void host_clear_audio();

// sends info for host to display
void host_send_info(InfoType type, char *str);

// gets emulator event, returns true if there is one
bool host_get_event(EmuCtlEvent *event);

// returns controller state
uint16_t host_get_controller();
