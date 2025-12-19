/*
 * clownmdemu on x11: minimum viable product
 * 
 * to run:
 * clownmdemu <file_name>
 * 
 * keyboard controls:
 * up    = up
 * down  = down
 * left  = left
 * right = right
 * q     = x
 * w     = y
 * e     = z
 * a     = a
 * s     = b
 * d     = c
 * f     = mode
 * enter = start
 * tab   = soft reset
 * esc   = exit
 */

/* for timespec and clock_gettime */
#define _POSIX_C_SOURCE 199309L

/* for realpath */
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <arpa/inet.h>

/* CLOCK_MONOTONIC_RAW is linux-only */
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

#ifndef DISABLE_AUDIO
#if defined(__linux__)
/* inline not available in c89, so stub it out */
#define inline
#include <pulse/simple.h>
#include <pulse/error.h>
#elif defined(__OpenBSD__)
#include <sndio.h>
#endif
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#define MIXER_IMPLEMENTATION
#include "common/core/clownmdemu.h"
#include "common/cd-reader.h"
#include "common/mixer.h"

#define BILLION 1000000000L
#define ROM_SIZE_MAX 0x800000
#define FRAMEBUFFER_SIZE VDP_MAX_SCANLINE_WIDTH * VDP_MAX_SCANLINES * sizeof(uint32_t)

enum
{
	REGION_UNSPECIFIED,
	REGION_US,
	REGION_JP,
	REGION_EU
};

/* path stuff */
char save_pwd[PATH_MAX];
char save_argv0[PATH_MAX];
char save_path[PATH_MAX];

const char path_sep = '/';
const char path_sep_str[2] = "/";
const char path_list_sep[8] = ":"; /* could be ":;" */

char exe_dir[PATH_MAX];

/* emulator stuff */
typedef struct emulator
{
	ClownMDEmu_Configuration configuration;
	ClownMDEmu_Constant constant;
	ClownMDEmu_State state;
	ClownMDEmu_Callbacks callbacks;
	ClownMDEmu clownmdemu;
	
	CDReader_State cd;
	ClownCD_FileCallbacks cd_callbacks;
	cc_bool cd_boot;
	
	cc_bool audio_init;
	Mixer_State mixer;
	cc_s16l samples[MIXER_MAXIMUM_AUDIO_FRAMES_PER_FRAME * MIXER_CHANNEL_COUNT];
	size_t audio_bytes;

	int rom_size;
	int width;
	int height;
	uint32_t colors[VDP_TOTAL_COLOURS];
	uint32_t * framebuffer;
	cc_bool buttons[2][CLOWNMDEMU_BUTTON_MAX];
	cc_u16l * rom_buf;
	char rom_regions[4]; /* includes '\0' at end */
	cc_bool log_enabled;
	
	FILE * bram;
} emulator;

/* utility functions */
void warn(const char * fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	printf("WARN: ");
	vprintf(fmt, args);
	va_end(args);
}

void usage(const char * app_name)
{
	printf(
		"Usage: %s [OPTIONS] FILE\n"
		"Options:\n"
		"        -h, -?     Print this help text\n"
		"        -r (U|J|E) Set region to US, Japan or Europe respectively\n"
		"        -l         Enable emulator core log output (disabled by default)\n"
		"        -w         Enable widescreen hack (disabled by default)\n",
		app_name
	);
}

int exe_dir_init(char * argv0, char * result, size_t result_size)
{
	char new_path[PATH_MAX];
	char new_path_2[PATH_MAX];
	char * dir_tmp;
	
	getcwd(save_pwd, sizeof(save_pwd));
	strncpy(save_argv0, argv0, sizeof(save_argv0));
	save_argv0[sizeof(save_argv0) - 1] = 0;
	strncpy(save_path, getenv("PATH"), sizeof(save_path));
	save_path[sizeof(save_path) - 1] = 0;
	
	result[0] = 0;
	if (save_argv0[0] == path_sep)
	{
		/* absolute path */
		realpath(save_argv0, new_path);
		if (!access(new_path, F_OK))
		{
			dir_tmp = dirname(new_path);
			strncpy(result, dir_tmp, result_size);
			result[result_size - 1] = 0;
			return 1;
		}
		else
		{
			perror("access 1");
		}
	}
	else if (strchr(save_argv0, path_sep))
	{
		/* relative path */
		strncpy(new_path_2, save_pwd, sizeof(new_path_2));
		new_path_2[sizeof(new_path_2) - 1] = 0;
		strncat(new_path_2, path_sep_str, sizeof(new_path_2) - 1);
		new_path_2[sizeof(new_path_2) - 1] = 0;
		strncat(new_path_2, save_argv0, sizeof(new_path_2) - 1);
		new_path_2[sizeof(new_path_2) - 1] = 0;
		realpath(new_path_2, new_path);
		if (!access(new_path, F_OK))
		{
			dir_tmp = dirname(new_path);
			strncpy(result, dir_tmp, result_size);
			result[result_size - 1] = 0;
			return 1;
		}
		else
		{
			perror("access 2");
		}
	}
	else
	{
		/* search $PATH */
		char * save_ptr;
		char * path_item;
		for (path_item = strtok_r(save_path, path_list_sep, &save_ptr); path_item; path_item = strtok_r(NULL, path_list_sep, &save_ptr))
		{
			strncpy(new_path_2, path_item, sizeof(new_path_2));
			new_path_2[sizeof(new_path_2) - 1] = 0;
			strncat(new_path_2, path_sep_str, sizeof(new_path_2) - 1);
			new_path_2[sizeof(new_path_2) - 1] = 0;
			strncat(new_path_2, save_argv0, sizeof(new_path_2) - 1);
			new_path_2[sizeof(new_path_2) - 1] = 0;
			realpath(new_path_2, new_path);
			if (!access(new_path, F_OK))
			{
				dir_tmp = dirname(new_path);
				strncpy(result, dir_tmp, result_size);
				result[result_size - 1] = 0;
				return 1;
			}
		}
		/* end for */
		perror("access 3");
	}
	/* if we have reached here, we have exhausted all methods, so give up */
	return 0;
}

char * build_file_path(const char * path, const char * filename)
{
	int ret_size;
	char * ret;
	if (!path || !filename)
	{
		return NULL;
	}
	if (path[0] == '\0' || filename[0] == '\0')
	{
		return NULL;
	}
	ret_size = PATH_MAX * sizeof(char);
	ret = (char *) malloc(ret_size);
	if (!ret)
	{
		return NULL;
	}
	strncpy(ret, path, ret_size - 1);
	ret[ret_size - 1] = 0;
	strncat(ret, path_sep_str, ret_size - 1);
	ret[ret_size - 1] = 0;
	strncat(ret, filename, ret_size - 1);
	ret[ret_size - 1] = 0;
	
	/* you must free file names after using them! */
	return ret;
}

/* emulator callbacks */
void emulator_callback_color_update(void * const data, const cc_u16f idx, const cc_u16f color)
{
	emulator * e = (emulator *) data;
	const cc_u32f r = color & 0xF;
	const cc_u32f g = color >> 4 & 0xF;
	const cc_u32f b = color >> 8 & 0xF;
	
	/* in ARGB8888 format */
	e->colors[idx] = 0xFF000000 | (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
}

void emulator_callback_scanline_render(void * const data, const cc_u16f scanline, const cc_u8l * const pixels, const cc_u16f left_boundary, const cc_u16f right_boundary, const cc_u16f width, const cc_u16f height)
{
	emulator * e = (emulator *) data;
	int i;
	const cc_u8l * input;
	uint32_t * output;
	e->width = width;
	e->height = height;
	input = pixels + left_boundary;
	output = &e->framebuffer[scanline * width + left_boundary];
	
	for (i = left_boundary; i < right_boundary; ++i)
	{
		*output++ = e->colors[*input++];
	}
}

cc_bool emulator_callback_input_request(void * const data, const cc_u8f player, const ClownMDEmu_Button button)
{
	emulator * e = (emulator *) data;
	return e->buttons[player][button];
}

void emulator_callback_fm_generate(void * const data, const struct ClownMDEmu * clownmdemu, size_t frames, void (* generate_fm_audio)(const struct ClownMDEmu * clownmdemu, cc_s16l * sample_buffer, size_t total_frames))
{
	emulator * e = (emulator *) data;
	generate_fm_audio(clownmdemu, Mixer_AllocateFMSamples(&e->mixer, frames), frames);
}

void emulator_callback_psg_generate(void * const data, const struct ClownMDEmu * clownmdemu, size_t frames, void (* generate_psg_audio)(const struct ClownMDEmu * clownmdemu, cc_s16l * sample_buffer, size_t total_samples))
{
	emulator * e = (emulator *) data;
	generate_psg_audio(clownmdemu, Mixer_AllocatePSGSamples(&e->mixer, frames), frames);
}

void emulator_callback_pcm_generate(void * const data, const struct ClownMDEmu * clownmdemu, size_t frames, void (* generate_pcm_audio)(const struct ClownMDEmu * clownmdemu, cc_s16l * sample_buffer, size_t total_frames))
{
	emulator * e = (emulator *) data;
	generate_pcm_audio(clownmdemu, Mixer_AllocatePCMSamples(&e->mixer, frames), frames);
}

void emulator_callback_cdda_generate(void * const data, const struct ClownMDEmu * clownmdemu, size_t frames, void (* generate_cdda_audio)(const struct ClownMDEmu * clownmdemu, cc_s16l * sample_buffer, size_t total_frames))
{
	emulator * e = (emulator *) data;
	generate_cdda_audio(clownmdemu, Mixer_AllocateCDDASamples(&e->mixer, frames), frames);
}

void emulator_callback_cd_seek(void * const data, cc_u32f idx)
{
	emulator * e = (emulator *) data;
	CDReader_SeekToSector(&e->cd, idx);
}

void emulator_callback_cd_sector_read(void * const data, cc_u16l * const buf)
{
	emulator * e = (emulator *) data;
	CDReader_ReadSector(&e->cd, buf);
}

cc_bool emulator_callback_cd_seek_track(void * const data, const cc_u16f idx, const ClownMDEmu_CDDAMode mode)
{
	emulator * e = (emulator *) data;
	CDReader_PlaybackSetting playback_setting;
	
	switch (mode)
	{
		case CLOWNMDEMU_CDDA_PLAY_ALL:
			playback_setting = CDREADER_PLAYBACK_ALL;
			break;
		case CLOWNMDEMU_CDDA_PLAY_ONCE:
			playback_setting = CDREADER_PLAYBACK_ONCE;
			break;
		case CLOWNMDEMU_CDDA_PLAY_REPEAT:
			playback_setting = CDREADER_PLAYBACK_REPEAT;
			break;
		default:
			warn("emulator_callback_cd_seek_track: unknown play mode %d\n", mode);
			return cc_false;
	}
	
	return CDReader_PlayAudio(&e->cd, idx, playback_setting);
}

size_t emulator_callback_cd_audio_read(void * const data, cc_s16l * const buf, const size_t frames)
{
	emulator * e = (emulator *) data;
	return CDReader_ReadAudio(&e->cd, buf, frames);
}

cc_bool emulator_callback_save_file_open_read(void * const data, const char * const filename)
{
	emulator * e = (emulator *) data;
	char * file_path = build_file_path(exe_dir, filename);
	if (!file_path)
	{
		return cc_false;
	}
	e->bram = fopen(file_path, "r+b");
	free(file_path);
	return e->bram ? cc_true : cc_false;
}

cc_s16f emulator_callback_save_file_read(void * const data)
{
	emulator * e = (emulator *) data;
	uint8_t byte;
	if (!e->bram)
	{
		return -1;
	}
	else
	{
		return fread(&byte, 1, 1, e->bram) < 1 ? -1 : byte;
	}
}

cc_bool emulator_callback_save_file_open_write(void * const data, const char * const filename)
{
	emulator * e = (emulator *) data;
	char * file_path = build_file_path(exe_dir, filename);
	if (!file_path)
	{
		return cc_false;
	}
	e->bram = fopen(file_path, "w+b");
	free(file_path);
	return e->bram ? cc_true : cc_false;
}

void emulator_callback_save_file_write(void * const data, const cc_u8f val)
{
	emulator * e = (emulator *) data;
	if (e->bram)
	{
		fwrite(&val, 1, 1, e->bram);
	}
}

void emulator_callback_save_file_close(void * const data)
{
	emulator * e = (emulator *) data;
	if (e->bram)
	{
		fclose(e->bram);
	}
}

cc_bool emulator_callback_save_file_remove(void * const data, const char * const filename)
{
	int status;
	char * file_path = build_file_path(exe_dir, filename);
	if (!file_path)
	{
		return cc_false;
	}
	status = remove(file_path);
	free(file_path);
	return status == 0 ? cc_true : cc_false;
}

cc_bool emulator_callback_save_file_size_obtain(void * const data, const char * const filename, size_t * const size)
{
	emulator * e = (emulator *) data;
	int file_size = 0;
	char * file_path = build_file_path(exe_dir, filename);
	if (!file_path)
	{
		return cc_false;
	}
	e->bram = fopen(file_path, "rb");
	free(file_path);
	if (e->bram)
	{
		fseek(e->bram, 0, SEEK_END);
		file_size = ftell(e->bram);
		fclose(e->bram);
		if (file_size > 0)
		{
			*size = file_size;
		}
	}
	return file_size > 0 ? cc_true : cc_false;
}

void emulator_callback_log(void * const data, const char * fmt, va_list args)
{
	emulator * e = (emulator *) data;
	if (e->log_enabled == cc_true)
	{
		printf("core: ");
		vprintf(fmt, args);
		printf("\n");
	}
}

void * emulator_callback_clowncd_open(const char * filename, ClownCD_FileMode mode)
{
	const char * open_mode;
	switch (mode)
	{
		case CLOWNCD_RB:
			open_mode = "rb";
			break;
		case CLOWNCD_WB:
			open_mode = "wb";
			break;
	}
	
	return fopen(filename, open_mode);
}

int emulator_callback_clowncd_close(void * const stream)
{
	return fclose((FILE *) stream);
}

size_t emulator_callback_clowncd_read(void * const buf, const size_t size, const size_t count, void * const stream)
{
	int64_t bytes;
	if (size == 0 || count == 0)
	{
		return 0;
	}
	
	bytes = fread(buf, size, count, (FILE *) stream);
	if (bytes < 0 || (uint64_t) bytes > (size_t) -1)
	{
		return 0;
	}
	
	return bytes;
}

size_t emulator_callback_clowncd_write(const void * const buf, const size_t size, const size_t count, void * const stream)
{
	int64_t bytes;
	if (size == 0 || count == 0)
	{
		return 0;
	}
	
	bytes = fwrite(buf, size, count, (FILE *) stream);
	if (bytes < 0 || (uint64_t) bytes > (size_t) -1)
	{
		return 0;
	}
	
	return bytes;
}

long emulator_callback_clowncd_tell(void * const stream)
{
	const int64_t pos = ftell((FILE *) stream);
	if (pos < 0 || pos > LONG_MAX)
	{
		return -1;
	}
	
	return pos;
}

int emulator_callback_clowncd_seek(void * const stream, const long pos, const ClownCD_FileOrigin origin)
{
	int seek_origin;
	switch (origin)
	{
		case CLOWNCD_SEEK_SET:
			seek_origin = SEEK_SET;
			break;
		case CLOWNCD_SEEK_CUR:
			seek_origin = SEEK_CUR;
			break;
		case CLOWNCD_SEEK_END:
			seek_origin = SEEK_END;
			break;
		default:
			return -1;
	}
	
	return fseek((FILE *) stream, pos, seek_origin) != 0 ? -1 : 0;
}

void emulator_callback_clowncd_log(void * const data, const char * const msg)
{
	emulator * e = (emulator *) data;
	if (e->log_enabled == cc_true)
	{
		printf("clowncd: %s\n", msg);
	}
}

void emulator_callback_mixer_complete(void * const data, const cc_s16l * samples, const size_t frames)
{
	emulator * e = (emulator *) data;
	size_t bytes = frames * sizeof(cc_s16l) * MIXER_CHANNEL_COUNT;
	if (frames == 0 || frames > MIXER_MAXIMUM_AUDIO_FRAMES_PER_FRAME)
	{
		return;
	}
	memcpy(e->samples, samples, bytes);
	e->audio_bytes = bytes;
}

/* emulator misc */
void emulator_init(emulator * emu)
{
	ClownMDEmu_Parameters_Initialise(&emu->clownmdemu, &emu->configuration, &emu->constant, &emu->state, &emu->callbacks);
	
	emu->callbacks.user_data = emu;
	emu->callbacks.colour_updated = emulator_callback_color_update;
	emu->callbacks.scanline_rendered = emulator_callback_scanline_render;
	emu->callbacks.input_requested = emulator_callback_input_request;
	emu->callbacks.fm_audio_to_be_generated = emulator_callback_fm_generate;
	emu->callbacks.psg_audio_to_be_generated = emulator_callback_psg_generate;
	emu->callbacks.pcm_audio_to_be_generated = emulator_callback_pcm_generate;
	emu->callbacks.cdda_audio_to_be_generated = emulator_callback_cdda_generate;
	emu->callbacks.cd_seeked = emulator_callback_cd_seek;
	emu->callbacks.cd_sector_read = emulator_callback_cd_sector_read;
	emu->callbacks.cd_track_seeked = emulator_callback_cd_seek_track;
	emu->callbacks.cd_audio_read = emulator_callback_cd_audio_read;
	emu->callbacks.save_file_opened_for_reading = emulator_callback_save_file_open_read;
	emu->callbacks.save_file_read = emulator_callback_save_file_read;
	emu->callbacks.save_file_opened_for_writing = emulator_callback_save_file_open_write;
	emu->callbacks.save_file_written = emulator_callback_save_file_write;
	emu->callbacks.save_file_closed = emulator_callback_save_file_close;
	emu->callbacks.save_file_removed = emulator_callback_save_file_remove;
	emu->callbacks.save_file_size_obtained = emulator_callback_save_file_size_obtain;
	
	emu->cd_callbacks.open = emulator_callback_clowncd_open;
	emu->cd_callbacks.close = emulator_callback_clowncd_close;
	emu->cd_callbacks.read = emulator_callback_clowncd_read;
	emu->cd_callbacks.write = emulator_callback_clowncd_write;
	emu->cd_callbacks.tell = emulator_callback_clowncd_tell;
	emu->cd_callbacks.seek = emulator_callback_clowncd_seek;
	
	ClownCD_SetErrorCallback(emulator_callback_clowncd_log, emu);
	ClownMDEmu_SetLogCallback(emulator_callback_log, emu);
	
	ClownMDEmu_Constant_Initialise(&emu->constant);
	ClownMDEmu_State_Initialise(&emu->state);
	CDReader_Initialise(&emu->cd);
}

void emulator_init_audio(emulator * emu)
{
	cc_bool pal = emu->configuration.general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL ? cc_true : cc_false;
	memset(emu->samples, 0, sizeof(emu->samples));
	emu->audio_init = Mixer_Initialise(&emu->mixer, pal);
	if (!emu->audio_init)
	{
		warn("audio init failed\n");
	}
}

void emulator_set_region(emulator * emu, int force_region)
{
	int detect_region = force_region;
	if (detect_region == REGION_UNSPECIFIED)
	{
		if (!emu->cd_boot)
		{
			if (emu->rom_size >= 0x1F3)
			{
				/* in order: us, japan then europe, otherwise fail */
				if (strchr(emu->rom_regions, 'U'))
				{
					detect_region = REGION_US;
				}
				else if (strchr(emu->rom_regions, 'J'))
				{
					detect_region = REGION_JP;
				}
				else if (strchr(emu->rom_regions, 'E'))
				{
					detect_region = REGION_EU;
				}
				else
				{
					warn("unable to autodetect region, defaulting to us\n");
				}
			}
			else
			{
				warn("rom too small to include region header info, defaulting to us\n");
			}
		}
		else
		{
			warn("region autodetection not implemented for cd mode\n");
		}
	}
	switch (detect_region)
	{
		case REGION_JP:
			emu->configuration.general.region = CLOWNMDEMU_REGION_DOMESTIC;
			emu->configuration.general.tv_standard = CLOWNMDEMU_TV_STANDARD_NTSC;
			break;
		case REGION_EU:
			emu->configuration.general.region = CLOWNMDEMU_REGION_OVERSEAS;
			emu->configuration.general.tv_standard = CLOWNMDEMU_TV_STANDARD_PAL;
			break;
		default:
		case REGION_US:
			emu->configuration.general.region = CLOWNMDEMU_REGION_OVERSEAS;
			emu->configuration.general.tv_standard = CLOWNMDEMU_TV_STANDARD_NTSC;
			break;
	}
}

void emulator_set_options(emulator * emu, cc_bool log_enabled, cc_bool widescreen_enabled)
{
	emu->log_enabled = log_enabled;
	if (widescreen_enabled == cc_true)
	{
		emu->configuration.vdp.widescreen_enabled = cc_true;
	}
}

void emulator_reset(emulator * emu)
{
	ClownMDEmu_Reset(&emu->clownmdemu, emu->cd_boot);
	/*printf("sram: size %d nv %d data_size %d type %d map_in %d\n",
		emu->state.external_ram.size,
		emu->state.external_ram.non_volatile,
		emu->state.external_ram.data_size,
		emu->state.external_ram.device_type,
		emu->state.external_ram.mapped_in
	);*/
}

void emulator_iterate(emulator * emu)
{
	if (emu->audio_init)
	{
		Mixer_Begin(&emu->mixer);
	}
	ClownMDEmu_Iterate(&emu->clownmdemu);
	if (emu->audio_init)
	{
		Mixer_End(&emu->mixer, emulator_callback_mixer_complete, emu);
	}
}

int emulator_load_file(emulator * emu, const char * filename)
{
	int i;
	int size;
	int alloc_size;
	FILE * f;
	struct stat buf;
	cc_u16l * tmp;
	
	if (stat(filename, &buf) != 0)
	{
		printf("stat failed\n");
		return 0;
	}
	
	if (!S_ISREG(buf.st_mode))
	{
		printf("not a file\n");
		return 0;
	}
	
	CDReader_Open(&emu->cd, NULL, filename, &emu->cd_callbacks);
	emu->cd_boot = CDReader_IsMegaCDGame(&emu->cd);
	
	if (emu->cd_boot)
	{
		if (!CDReader_SeekToSector(&emu->cd, 0))
		{
			printf("cd sector seek failed\n");
			CDReader_Close(&emu->cd);
			return 0;
		}
		else
		{
			printf("booting cd\n");
			return 1;
		}
	}
	else
	{
		CDReader_Close(&emu->cd);
		f = fopen(filename, "rb");
		if (!f)
		{
			perror("unable to open file");
			return 0;
		}
		if (fseek(f, 0, SEEK_END) != 0)
		{
			printf("unable to seek to end of file\n");
			fclose(f);
			return 0;
		}
		size = ftell(f);
		if (size > ROM_SIZE_MAX || size < 1)
		{
			printf("size out of bounds\n");
			fclose(f);
			return 0;
		}
		if (size % 2 == 1)
		{
			alloc_size = size + 1;
		}
		else
		{
			alloc_size = size;
		}
		tmp = (cc_u16l *) malloc(alloc_size);
		if (!tmp)
		{
			printf("unable to allocate rom buffer");
			fclose(f);
			return 0;
		}
		if (fseek(f, 0, SEEK_SET) != 0)
		{
			printf("unable to seek to start of file\n");
			fclose(f);
			return 0;
		}
		if (fread(tmp, 1, size, f) != size)
		{
			/* error handling */
			if (feof(f))
			{
				printf("unexpected EOF\n");
			}
			else if (ferror(f))
			{
				printf("unable to read file\n");
			}
			else
			{
				printf("unknown read error\n");
			}
			fclose(f);
			return 0;
		}
		emu->rom_size = size;
		memset(emu->rom_regions, 0, sizeof(emu->rom_regions));
		if (emu->rom_size >= 0x1F3)
		{
			memcpy(emu->rom_regions, &tmp[0x1F0 / sizeof(cc_u16l)], 3);
		}
		/* byteswap the rom so the emulator core can read it */
		for (i = 0; i < alloc_size / sizeof(cc_u16l); i++)
		{
			tmp[i] = ((tmp[i] & 0xFF) << 8) | ((tmp[i] & 0xFF00) >> 8); 
		}
		if (emu->rom_buf)
		{
			free(emu->rom_buf);
		}
		emu->rom_buf = tmp;
		ClownMDEmu_SetCartridge(&emu->clownmdemu, emu->rom_buf, emu->rom_size);
		printf("booting cartridge, loaded %d bytes\n", size);
		fclose(f);
	}
	
	return 1;
}

void emulator_key(emulator * emu, int keysym, cc_bool down)
{
	switch (keysym)
	{
		case XK_Up:
			emu->buttons[0][CLOWNMDEMU_BUTTON_UP] = down;
			break;
		case XK_Down:
			emu->buttons[0][CLOWNMDEMU_BUTTON_DOWN] = down;
			break;
		case XK_Left:
			emu->buttons[0][CLOWNMDEMU_BUTTON_LEFT] = down;
			break;
		case XK_Right:
			emu->buttons[0][CLOWNMDEMU_BUTTON_RIGHT] = down;
			break;
		case XK_q:
			emu->buttons[0][CLOWNMDEMU_BUTTON_X] = down;
			break;
		case XK_w:
			emu->buttons[0][CLOWNMDEMU_BUTTON_Y] = down;
			break;
		case XK_e:
			emu->buttons[0][CLOWNMDEMU_BUTTON_Z] = down;
			break;
		case XK_a:
			emu->buttons[0][CLOWNMDEMU_BUTTON_A] = down;
			break;
		case XK_s:
			emu->buttons[0][CLOWNMDEMU_BUTTON_B] = down;
			break;
		case XK_d:
			emu->buttons[0][CLOWNMDEMU_BUTTON_C] = down;
			break;
		case XK_f:
			emu->buttons[0][CLOWNMDEMU_BUTTON_MODE] = down;
			break;
		case XK_Return:
			emu->buttons[0][CLOWNMDEMU_BUTTON_START] = down;
			break;
	}
}

void emulator_shutdown(emulator * emu)
{
	CDReader_Deinitialise(&emu->cd);
	if (emu->audio_init)
	{
		Mixer_Deinitialise(&emu->mixer);
		emu->audio_init = cc_false;
	}
	if (emu->rom_buf)
	{
		free(emu->rom_buf);
		emu->rom_buf = NULL;
	}
}

/* init and main loop */
int main(int argc, char ** argv)
{
	long ns_desired;
	
	emulator * emu;
	
	cc_bool log_enabled;
	cc_bool widescreen_enabled;
	int width;
	int height;
	int region;
	const char * filename;
	int i;
	int running;
	
	int root;
	int default_screen;
	int bit_depth;
	XVisualInfo vis_info;
	XSetWindowAttributes window_attr;
	unsigned long attr_mask;
	Window window;
	Atom wm_delete_window;
	Display * display;
	XImage * x_window_buffer;
	GC default_gc;
	XSizeHints hints;
#ifndef DISABLE_AUDIO
#if defined(__linux__)
	pa_simple * audio_device;
	pa_sample_spec audio_params;
	int audio_error;
#elif defined(__OpenBSD__)
	struct sio_hdl * audio_device;
	struct sio_par audio_params;
#endif
#endif
	
	if (argc < 2)
	{
		usage(argv[0]);
		return 1;
	}
	
	log_enabled = cc_false;
	widescreen_enabled = cc_false;
	region = REGION_UNSPECIFIED;
	filename = NULL;
	
	/*
	 * parse args
	 * argv[0] is the program name, so start at 1
	 */
	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-')
		{
			/* flag */
			if (strlen(argv[i]) < 2)
			{
				printf("invalid empty flag\n");
				return 1;
			}
			
			switch (argv[i][1])
			{
				case 'h':
				case '?':
					usage(argv[0]);
					return 0;
				case 'r':
					if (i == argc - 1)
					{
						printf("unexpected end of args\n");
						return 1;
					}
					else
					{
						i++;
						switch (argv[i][0])
						{
							case 'u':
							case 'U':
								region = REGION_US;
								break;
							case 'j':
							case 'J':
								region = REGION_JP;
								break;
							case 'e':
							case 'E':
								region = REGION_EU;
								break;
							default:
								printf("region must be u, j, or e\n");
								return 1;
						}
					}
					break;
				case 'l':
					log_enabled = cc_true;
					break;
				case 'w':
					widescreen_enabled = cc_true;
					break;
				default:
					printf("unknown flag %s\n", argv[i]);
					usage(argv[0]);
					return 1;
			}
		}
		else
		{
			/* file name */
			if (!filename)
			{
				filename = argv[i];
			}
			else
			{
				printf("specify only 1 filename\n");
				return 1;
			}
		}
	}
	
	if (!filename)
	{
		printf("no filename specified\n");
		return 1;
	}
	
	if (!exe_dir_init(argv[0], exe_dir, sizeof(exe_dir)))
	{
		warn("unable to get executable directory, saves will not be available!\n");
	}
	
	width = widescreen_enabled == cc_true ? VDP_MAX_SCANLINE_WIDTH - (VDP_TILE_PAIR_COUNT * VDP_TILE_WIDTH) : VDP_H40_SCREEN_WIDTH_IN_TILE_PAIRS * VDP_TILE_PAIR_WIDTH;
	height = VDP_MAX_SCANLINES;
	
	emu = (emulator *) malloc(sizeof(emulator));
	if (!emu)
	{
		printf("unable to alloc emu\n");
		return 1;
	}
	
	emu->framebuffer = (uint32_t *) malloc(FRAMEBUFFER_SIZE);
	if (!emu->framebuffer)
	{
		printf("unable to alloc internal framebuffer\n");
		return 1;
	}
	
	/* init window */
	bit_depth = 24;
	attr_mask = CWBackPixel | CWColormap | CWEventMask;
	display = XOpenDisplay(0);
	if (!display)
	{
		printf("unable to open display\n");
		return 1;
	}
	root = DefaultRootWindow(display);
	default_screen = DefaultScreen(display);
	if (!XMatchVisualInfo(display, default_screen, bit_depth, TrueColor, &vis_info))
	{
		printf("no matching visual info\n");
		return 1;
	}
	window_attr.background_pixel = 0;
	window_attr.colormap = XCreateColormap(display, root, vis_info.visual, AllocNone);
	window_attr.event_mask = KeyPressMask | KeyReleaseMask;
	window = XCreateWindow(display, root, 0, 0, width, height, 0, vis_info.depth, InputOutput, vis_info.visual, attr_mask, &window_attr);
	if (!window)
	{
		printf("unable to create window\n");
		return 1;
	}
	XStoreName(display, window, "clownmdemu");
	hints.flags = PMinSize | PMaxSize;
	hints.min_width = width;
	hints.min_height = height;
	hints.max_width = width;
	hints.max_height = height;
	XSetWMNormalHints(display, window, &hints);
	XMapWindow(display, window);
	XFlush(display);
	x_window_buffer = XCreateImage(display, vis_info.visual, vis_info.depth, ZPixmap, 0, (char *) emu->framebuffer, width, height, 32, 0);
	if (!x_window_buffer)
	{
		printf("unable to create window image\n");
		return 1;
	}
	default_gc = DefaultGC(display, default_screen);
	wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
	if (!XSetWMProtocols(display, window, &wm_delete_window, 1))
	{
		printf("unable to intercept window close event\n");
		return 1;
	}

#ifndef DISABLE_AUDIO
	/* init audio */
#if defined(__linux__)
	audio_params.format = PA_SAMPLE_S16LE;
	audio_params.channels = MIXER_CHANNEL_COUNT;
	audio_params.rate = MIXER_OUTPUT_SAMPLE_RATE;
	audio_device = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK, NULL, "audio", &audio_params, NULL, NULL, &audio_error);
	if (!audio_device)
	{
		printf("unable to create audio device: %s\n", pa_strerror(audio_error));
		return 1;
	}
#elif defined(__OpenBSD__)
	audio_device = sio_open(SIO_DEVANY, SIO_PLAY, 0);
	if (!audio_device)
	{
		printf("unable to open audio device\n");
		return 1;
	}
	sio_initpar(&audio_params);
	audio_params.bits = 16;
	audio_params.bps = SIO_BPS(16);
	audio_params.le = SIO_LE_NATIVE;
	audio_params.pchan = MIXER_CHANNEL_COUNT;
	audio_params.rate = MIXER_OUTPUT_SAMPLE_RATE;
	audio_params.xrun = SIO_IGNORE;
	if (!sio_setpar(audio_device, &audio_params))
	{
		printf("unable to set audio properties\n");
		return 1;
	}
	if (!sio_start(audio_device))
	{
		printf("unable to start audio device\n");
		return 1;
	}
#endif
#endif
	
	/* init emu */
	emulator_init(emu);
	emulator_set_options(emu, log_enabled, widescreen_enabled);
	if (!emulator_load_file(emu, filename))
	{
		printf("unable to load file\n");
		return 1;
	}
	emulator_set_region(emu, region);
	emulator_init_audio(emu);
	emulator_reset(emu);
	
	ns_desired = BILLION / (emu->configuration.general.tv_standard == CLOWNMDEMU_TV_STANDARD_NTSC ? 60.0f : 50.0f);
	running = 1;
	/* main loop */
	while (running)
	{
		static long ns_elapsed, ns_delta;
		static struct timespec start_timespec, end_timespec, sleep_timespec;
		XEvent ev;
		
		ns_elapsed = ns_delta = 0;
		sleep_timespec.tv_sec = sleep_timespec.tv_nsec = 0;
		clock_gettime(CLOCK_MONOTONIC_RAW, &start_timespec);
		
		while (XPending(display) > 0)
		{
			XClientMessageEvent * ec;
			XKeyPressedEvent * ek;
			int keysym;
			
			XNextEvent(display, &ev);
			switch (ev.type)
			{
				case ClientMessage:
					ec = (XClientMessageEvent *) &ev;
					if ((Atom) ec->data.l[0] == wm_delete_window)
					{
						running = 0;
					}
					break;
				case KeyPress:
					ek = (XKeyPressedEvent *) &ev;
					keysym = XkbKeycodeToKeysym(display, ek->keycode, 0, 0);
					if (keysym == XK_Escape)
					{
						running = 0;
					}
					else if (keysym == XK_Tab)
					{
						emulator_reset(emu);
					}
					else
					{
						emulator_key(emu, keysym, cc_true);
					}
					break;
				case KeyRelease:
					ek = (XKeyPressedEvent *) &ev;
					keysym = XkbKeycodeToKeysym(display, ek->keycode, 0, 0);
					emulator_key(emu, keysym, cc_false);
					break;
			}
		}
		
		memset(emu->framebuffer, 0, FRAMEBUFFER_SIZE);
		
		emulator_iterate(emu);
		
		if (emu->width > 0 && emu->height > 0)
		{
			x_window_buffer->width = emu->width;
			x_window_buffer->height = emu->height;
			x_window_buffer->bytes_per_line = emu->width * 4;
			XClearWindow(display, window);
			XPutImage(display, window, default_gc, x_window_buffer, 0, 0, (width - emu->width) / 2, (height - emu->height) / 2, width, height);
		}
		
#ifndef DISABLE_AUDIO
#if defined(__linux__)
		pa_simple_write(audio_device, emu->samples, emu->audio_bytes, &audio_error);
#elif defined(__OpenBSD__)
		sio_write(audio_device, emu->samples, emu->audio_bytes);
#endif
#endif
		
		clock_gettime(CLOCK_MONOTONIC_RAW, &end_timespec);
		if (end_timespec.tv_sec - start_timespec.tv_sec == 0)
		{
			ns_elapsed = end_timespec.tv_nsec - start_timespec.tv_nsec;
			ns_delta = ns_desired - ns_elapsed;
			/*printf("%ld %ld\n", ns_elapsed, ns_delta);*/
			if (ns_elapsed < ns_desired)
			{
				sleep_timespec.tv_nsec = ns_delta;
				nanosleep(&sleep_timespec, NULL);
			}
		}
	}
	
	XDestroyWindow(display, window);
	XCloseDisplay(display);
	
	emulator_shutdown(emu);
	free(emu);
#ifndef DISABLE_AUDIO
#if defined(__linux__)
	pa_simple_drain(audio_device, &audio_error);
	pa_simple_free(audio_device);
#elif defined(__OpenBSD__)
	sio_stop(audio_device);
	sio_close(audio_device);
#endif
#endif
	return 0;
}
