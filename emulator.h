#ifndef EMULATOR_H
#define EMULATOR_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "common/core/clownmdemu.h"
#include "common/cd-reader.h"
#include "common/mixer.h"

typedef uint32_t palette[VDP_TOTAL_COLOURS];

typedef enum region
{
	REGION_UNSPECIFIED,
	REGION_US,
	REGION_JP,
	REGION_EU
} region;

typedef struct emulator
{
	ClownMDEmu_InitialConfiguration initial_configuration;
	ClownMDEmu_Callbacks callbacks;
	ClownMDEmu clownmdemu;
	
	CDReader_State cd;
	ClownCD_FileCallbacks cd_callbacks;
	
	ClownMDEmu_StateBackup state_backup;
	CDReader_StateBackup cd_backup;
	palette colors_backup;
	
	cc_bool audio_init;
	Mixer_State mixer;
	cc_s16l * samples;
	size_t audio_bytes;

	int rom_size;
	int width;
	int height;
	palette colors;
	uint32_t * framebuffer;
	cc_bool buttons[2][CLOWNMDEMU_BUTTON_MAX];
	cc_u16l * rom_buf;
	char rom_regions[4]; /* includes '\0' at end */
	char cd_regions[4]; /* same thing */
	cc_bool log_enabled;
	
	FILE * bram;
	char * cartridge_filename;
	char * cd_filename;
	cc_bool cartridge_has_save_ram;
	cc_bool cartridge_inserted;
	cc_bool cd_inserted;
} emulator;

void warn(const char * fmt, ...);

void emulator_init(emulator * emu);
void emulator_init_audio(emulator * emu);
void emulator_set_region(emulator * emu, region force_region);
void emulator_set_options(emulator * emu, cc_bool log_enabled, cc_bool widescreen_enabled);
void emulator_reset(emulator * emu, cc_bool hard);
void emulator_iterate(emulator * emu);
int emulator_load_file(emulator * emu, const char * filename);
int emulator_load_cartridge(emulator * emu, const char * filename);
void emulator_unload_cartridge(emulator * emu);
int emulator_load_cd(emulator * emu, const char * filename);
void emulator_unload_cd(emulator * emu);
void emulator_load_sram(emulator * emu);
void emulator_save_sram(emulator * emu);
void emulator_load_state(emulator * emu, const char * filename);
void emulator_save_state(emulator * emu);
void emulator_shutdown_audio(emulator * emu);
void emulator_shutdown(emulator * emu);

#endif /* EMULATOR_H */
