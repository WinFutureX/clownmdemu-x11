#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

#define MIXER_IMPLEMENTATION

#include "emulator.h"
#include "file.h"
#include "path.h"

const char save_state_magic[8] = "CMDEFSS";
const size_t save_state_size = sizeof(save_state_magic) + sizeof(ClownMDEmu_StateBackup) + sizeof(CDReader_StateBackup) + sizeof(palette);

/* TODO: deal with these */
#define ROM_SIZE_MAX 0x800000
#define SAMPLE_BUFFER_SIZE MIXER_MAXIMUM_AUDIO_FRAMES_PER_FRAME * MIXER_CHANNEL_COUNT * sizeof(cc_s16l)

/* TODO: move this somewhere else */
void warn(const char * fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	printf("WARN: ");
	vprintf(fmt, args);
	va_end(args);
}

/* callbacks */

static void emulator_callback_color_update(void * data, cc_u16f idx, cc_u16f color)
{
	emulator * e = (emulator *) data;
	const cc_u32f r = color & 0xF;
	const cc_u32f g = color >> 4 & 0xF;
	const cc_u32f b = color >> 8 & 0xF;
	
	/* in ARGB8888 format */
	e->colors[idx] = 0xFF000000 | (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
}

static void emulator_callback_scanline_render(void * data, cc_u16f scanline, const cc_u8l * pixels, cc_u16f left_boundary, cc_u16f right_boundary, cc_u16f width, cc_u16f height)
{
	emulator * e = (emulator *) data;
	cc_u16f i;
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

static cc_bool emulator_callback_input_request(void * data, cc_u8f player, ClownMDEmu_Button button)
{
	emulator * e = (emulator *) data;
	return e->buttons[player][button];
}

static void emulator_callback_fm_generate(void * data, struct ClownMDEmu * clownmdemu, size_t frames, void (* generate_fm_audio)(struct ClownMDEmu * clownmdemu, cc_s16l * sample_buffer, size_t total_frames))
{
	emulator * e = (emulator *) data;
	generate_fm_audio(clownmdemu, Mixer_AllocateFMSamples(&e->mixer, frames), frames);
}

static void emulator_callback_psg_generate(void * data, struct ClownMDEmu * clownmdemu, size_t frames, void (* generate_psg_audio)(struct ClownMDEmu * clownmdemu, cc_s16l * sample_buffer, size_t total_samples))
{
	emulator * e = (emulator *) data;
	generate_psg_audio(clownmdemu, Mixer_AllocatePSGSamples(&e->mixer, frames), frames);
}

static void emulator_callback_pcm_generate(void * data, struct ClownMDEmu * clownmdemu, size_t frames, void (* generate_pcm_audio)(struct ClownMDEmu * clownmdemu, cc_s16l * sample_buffer, size_t total_frames))
{
	emulator * e = (emulator *) data;
	generate_pcm_audio(clownmdemu, Mixer_AllocatePCMSamples(&e->mixer, frames), frames);
}

static void emulator_callback_cdda_generate(void * data, struct ClownMDEmu * clownmdemu, size_t frames, void (* generate_cdda_audio)(struct ClownMDEmu * clownmdemu, cc_s16l * sample_buffer, size_t total_frames))
{
	emulator * e = (emulator *) data;
	generate_cdda_audio(clownmdemu, Mixer_AllocateCDDASamples(&e->mixer, frames), frames);
}

static void emulator_callback_cd_seek(void * data, cc_u32f idx)
{
	emulator * e = (emulator *) data;
	CDReader_SeekToSector(&e->cd, idx);
}

static void emulator_callback_cd_sector_read(void * data, cc_u16l * buf)
{
	emulator * e = (emulator *) data;
	CDReader_ReadSector(&e->cd, buf);
}

static cc_bool emulator_callback_cd_seek_track(void * data, cc_u16f idx, ClownMDEmu_CDDAMode mode)
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

static size_t emulator_callback_cd_audio_read(void * data, cc_s16l * buf, size_t frames)
{
	emulator * e = (emulator *) data;
	return CDReader_ReadAudio(&e->cd, buf, frames);
}

static cc_bool emulator_callback_save_file_open_read(void * data, const char * filename)
{
	emulator * e = (emulator *) data;
	char * file_path = build_file_path(get_exe_dir(), filename);
	if (!file_path)
	{
		return cc_false;
	}
	e->bram = file_open_read(file_path);
	free(file_path);
	return e->bram ? cc_true : cc_false;
}

static cc_s16f emulator_callback_save_file_read(void * data)
{
	emulator * e = (emulator *) data;
	unsigned char byte;
	if (!e->bram)
	{
		return -1;
	}
	else
	{
		return file_read(&byte, sizeof(byte), e->bram) < sizeof(byte) ? -1 : byte;
	}
}

static cc_bool emulator_callback_save_file_open_write(void * data, const char * filename)
{
	emulator * e = (emulator *) data;
	char * file_path = build_file_path(get_exe_dir(), filename);
	if (!file_path)
	{
		return cc_false;
	}
	e->bram = file_open_truncate(file_path);
	free(file_path);
	return e->bram ? cc_true : cc_false;
}

static void emulator_callback_save_file_write(void * data, cc_u8f val)
{
	emulator * e = (emulator *) data;
	if (e->bram)
	{
		file_write(&val, sizeof(unsigned char), e->bram);
	}
}

static void emulator_callback_save_file_close(void * data)
{
	emulator * e = (emulator *) data;
	if (e->bram)
	{
		file_close(e->bram);
	}
}

static cc_bool emulator_callback_save_file_remove(void * data, const char * filename)
{
	int status;
	char * file_path = build_file_path(get_exe_dir(), filename);
	(void) data;
	if (!file_path)
	{
		return cc_false;
	}
	status = remove(file_path);
	free(file_path);
	return status == 0 ? cc_true : cc_false;
}

static cc_bool emulator_callback_save_file_size_obtain(void * data, const char * filename, size_t * size)
{
	emulator * e = (emulator *) data;
	int file_size = 0;
	char * file_path = build_file_path(get_exe_dir(), filename);
	if (!file_path)
	{
		return cc_false;
	}
	e->bram = file_open_read(file_path);
	free(file_path);
	if (e->bram)
	{
		file_seek(e->bram, 0, SEEK_END);
		file_size = file_tell(e->bram);
		file_close(e->bram);
		e->bram = NULL;
		if (file_size > 0)
		{
			*size = file_size;
		}
	}
	return file_size > 0 ? cc_true : cc_false;
}

static void emulator_callback_log(void * data, const char * fmt, va_list args)
{
	emulator * e = (emulator *) data;
	if (e->log_enabled == cc_true)
	{
		printf("core: ");
		vprintf(fmt, args);
		printf("\n");
	}
}

static void * emulator_callback_clowncd_open(const char * filename, ClownCD_FileMode mode)
{
	switch (mode)
	{
		case CLOWNCD_RB:
			return file_open_read(filename);
			break;
		case CLOWNCD_WB:
			return file_open_write(filename);
			break;
		default:
			return NULL;
			break;
	}
}

static int emulator_callback_clowncd_close(void * stream)
{
	return file_close((FILE *) stream);
}

static size_t emulator_callback_clowncd_read(void * buf, size_t size, size_t count, void * stream)
{
	/* file_read() does not work here as it breaks cd reads for some reason */
	if (!buf || size == 0 || count == 0 || !stream)
	{
		return 0;
	}
	return fread(buf, size, count, (FILE *) stream);
}

static size_t emulator_callback_clowncd_write(const void * buf, size_t size, size_t count, void * stream)
{
	if (!buf || size == 0 || count == 0 || !stream)
	{
		return 0;
	}
	
	return fwrite(buf, size, count, (FILE *) stream);
}

static long emulator_callback_clowncd_tell(void * stream)
{
	return file_tell((FILE *) stream);
}

static int emulator_callback_clowncd_seek(void * stream, long pos, ClownCD_FileOrigin origin)
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
	
	return file_seek((FILE *) stream, pos, seek_origin) ? 0 : -1;
}

static void emulator_callback_clowncd_log(void * data, const char * msg)
{
	emulator * e = (emulator *) data;
	if (e->log_enabled == cc_true)
	{
		printf("clowncd: %s\n", msg);
	}
}

static void emulator_callback_mixer_complete(void * data, const cc_s16l * samples, size_t frames)
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

/* utility functions */

void emulator_init(emulator * emu)
{
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
	
	ClownMDEmu_Constant_Initialise();
	CDReader_Initialise(&emu->cd);
	ClownMDEmu_Initialise(&emu->clownmdemu, &emu->initial_configuration, &emu->callbacks);
}

void emulator_init_audio(emulator * emu)
{
	cc_bool pal = emu->clownmdemu.configuration.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL ? cc_true : cc_false;
	emu->samples = (cc_s16l *) malloc(SAMPLE_BUFFER_SIZE);
	if (!emu->samples)
	{
		warn("unable to alloc sample buffer\n");
		emu->audio_init = cc_false;
		return;
	}
	emu->audio_init = Mixer_Initialise(&emu->mixer, pal);
	if (!emu->audio_init)
	{
		warn("audio init failed\n");
	}
	memset(emu->samples, 0, SAMPLE_BUFFER_SIZE);
}

void emulator_set_region(emulator * emu, region force_region)
{
	region detect_region = force_region;
	if (detect_region == REGION_UNSPECIFIED)
	{
		if (emu->cd_boot || emu->rom_size >= 0x1F3)
		{
			char * region_list = emu->cd_boot ? emu->cd_regions : emu->rom_regions;
			/*
			 * in order: us, japan then europe, otherwise fail
			 * first we try the old style
			 */
			if (strchr(region_list, 'U'))
			{
				detect_region = REGION_US;
			}
			else if (strchr(region_list, 'J'))
			{
				detect_region = REGION_JP;
			}
			else if (strchr(region_list, 'E'))
			{
				detect_region = REGION_EU;
			}
			else
			{
				/*
				 * new style
				 * https://plutiedev.com/rom_header
				 */
				switch (region_list[0])
				{
					case '4':
					case '5':
					case '6':
					case '7':
					case 'C':
					case 'D':
					/* case 'E' already covered by old style checker */
					case 'F':
						detect_region = REGION_US;
						break;
					case '1':
					case '3':
					case '9':
					case 'B':
						detect_region = REGION_JP;
						break;
					case '8':
					case 'A':
						detect_region = REGION_EU;
						break;
					default:
						warn("unable to autodetect region, defaulting to us\n");
						break;
				}
			}
		}
		else
		{	
			warn("rom too small to include region header info, defaulting to us\n");
		}
	}
	switch (detect_region)
	{
		case REGION_JP:
			emu->clownmdemu.configuration.region = CLOWNMDEMU_REGION_DOMESTIC;
			emu->clownmdemu.configuration.tv_standard = CLOWNMDEMU_TV_STANDARD_NTSC;
			break;
		case REGION_EU:
			emu->clownmdemu.configuration.region = CLOWNMDEMU_REGION_OVERSEAS;
			emu->clownmdemu.configuration.tv_standard = CLOWNMDEMU_TV_STANDARD_PAL;
			break;
		default:
		case REGION_US:
			emu->clownmdemu.configuration.region = CLOWNMDEMU_REGION_OVERSEAS;
			emu->clownmdemu.configuration.tv_standard = CLOWNMDEMU_TV_STANDARD_NTSC;
			break;
	}
}

void emulator_set_options(emulator * emu, cc_bool log_enabled, cc_bool widescreen_enabled)
{
	emu->log_enabled = log_enabled;
	emu->clownmdemu.vdp.configuration.widescreen_tiles = widescreen_enabled == cc_true ? VDP_MAX_WIDESCREEN_TILES : 0;
}

void emulator_reset(emulator * emu, cc_bool hard)
{
	if (hard)
	{
		ClownMDEmu_HardReset(&emu->clownmdemu, !emu->cd_boot, emu->cd_boot);
		emu->cartridge_has_save_ram = emu->clownmdemu.state.external_ram.non_volatile;
	}
	else
	{
		ClownMDEmu_SoftReset(&emu->clownmdemu, !emu->cd_boot, emu->cd_boot);
	}
	/*printf("sram: size %ld nv %d data_size %d type %d map_in %d\n",
		emu->clownmdemu.state.external_ram.size,
		emu->clownmdemu.state.external_ram.non_volatile,
		emu->clownmdemu.state.external_ram.data_size,
		emu->clownmdemu.state.external_ram.device_type,
		emu->clownmdemu.state.external_ram.mapped_in
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
	if (!file_exists(filename))
	{
		printf("emulator_load_file: %s does not exist\n", filename);
		return 0;
	}
	
	if (!file_is_file(filename))
	{
		printf("emulator_load_file: %s is not a file\n", filename);
		return 0;
	}
	
	if (emulator_load_cd(emu, filename))
	{
		if (CDReader_IsMegaCDGame(&emu->cd))
		{
			emu->cd_boot = cc_true;
			printf("booting cd\n");
			return 1;
		}
		emulator_unload_cd(emu);
		emu->cd_boot = cc_false;
	}
	return emulator_load_cartridge(emu, filename);
}

int emulator_load_cartridge(emulator * emu, const char * filename)
{
	unsigned int i;
	long size;
	size_t alloc_size;
	cc_u16l * tmp;
	char * file;
	
	size = file_size(filename);
	if (size == -1)
	{
		printf("emulator_load_cartridge: size error\n");
		return 0;
	}
	else if (size > ROM_SIZE_MAX)
	{
		printf("emulator_load_cartridge: size exceeds bounds\n");
		return 0;
	}
	
	alloc_size = size % 2 == 1 ? size + 1 : size;
	
	if (!file_load_to_buffer(filename, (unsigned char **) &tmp, &alloc_size))
	{
		printf("emulator_load_cartridge: load error\n");
		return 0;
	}
	
	emu->rom_size = size;
	memset(emu->rom_regions, 0, sizeof(emu->rom_regions));
	if (emu->rom_size >= 0x1F3)
	{
		memcpy(emu->rom_regions, &tmp[0x1F0 / sizeof(cc_u16l)], 3);
		emu->rom_regions[3] = 0;
	}
	/* byteswap the rom so the emulator core can read it */
	for (i = 0; i < alloc_size / sizeof(cc_u16l); i++)
	{
		tmp[i] = ((tmp[i] & 0xFF) << 8) | ((tmp[i] & 0xFF00) >> 8); 
	}
	if (emu->rom_buf)
	{
		emulator_unload_cartridge(emu);
	}
	emu->rom_buf = tmp;
	ClownMDEmu_SetCartridge(&emu->clownmdemu, emu->rom_buf, emu->rom_size);
	printf("booting cartridge, loaded %ld bytes\n", size);
	file = strdup(filename);
	emu->cartridge_filename = get_basename(file);
	free(file);
	emulator_load_sram(emu);
	return 1;
}

void emulator_unload_cartridge(emulator * emu)
{
	if (emu->rom_buf)
	{
		free(emu->rom_buf);
		emu->rom_buf = NULL;
		ClownMDEmu_SetCartridge(&emu->clownmdemu, NULL, 0);
	}
	if (emu->cartridge_filename)
	{
		emulator_save_sram(emu);
		free(emu->cartridge_filename);
		emu->cartridge_filename = NULL;
	}
}

int emulator_load_cd(emulator * emu, const char * filename)
{
	char * tmp;
	unsigned char mcd_header[CDREADER_SECTOR_SIZE];
	CDReader_Open(&emu->cd, NULL, filename, &emu->cd_callbacks);
	if (!CDReader_IsOpen(&emu->cd))
	{
		return 0;
	}
	CDReader_SeekToSector(&emu->cd, 0);
	if (CDReader_ReadMegaCDHeaderSector(&emu->cd, mcd_header))
	{
		memcpy(emu->cd_regions, &mcd_header[0x1F0], 3);
		emu->cd_regions[3] = 0;
	}
	else
	{
		memset(emu->cd_regions, 0, sizeof(emu->cd_regions));
	}
	tmp = strdup(filename);
	if (tmp)
	{
		emu->cd_filename = get_basename(tmp);
		free(tmp);
	}
	return 1;
}

void emulator_unload_cd(emulator * emu)
{
	if (CDReader_IsOpen(&emu->cd))
	{
		CDReader_Close(&emu->cd);
	}
	if (emu->cd_filename)
	{
		free(emu->cd_filename);
		emu->cd_filename = NULL;
	}
}

void emulator_load_sram(emulator * emu)
{
	size_t size;
	char * path;
	char * comb;
	char * strip;
	cc_u8l * tmp;
	strip = strip_ext(emu->cartridge_filename);
	comb = append_ext(strip, "srm");
	path = build_file_path(get_exe_dir(), comb);
	if (path)
	{
		if (file_exists(path))
		{
			size = file_size(path);
			if (size > sizeof(emu->clownmdemu.state.external_ram.buffer))
			{
				printf("emulator_load_sram: cartridge save ram size exceeds bounds\n");
				return;
			}
			if (file_load_to_buffer(path, &tmp, &size))
			{
				memcpy(emu->clownmdemu.state.external_ram.buffer, tmp, size);
				free(tmp);
			}
			else
			{
				printf("emulator_load_sram: load error\n");
				return;
			}
		}
	}
	free(path);
	free(comb);
	free(strip);
}

void emulator_save_sram(emulator * emu)
{
	FILE * f;
	char * path;
	char * comb;
	char * strip;
	if (emu->cartridge_has_save_ram == cc_false || emu->clownmdemu.state.external_ram.size == 0)
	{
		return;
	}
	strip = strip_ext(emu->cartridge_filename);
	comb = append_ext(strip, "srm");
	path = build_file_path(get_exe_dir(), comb);
	if (path)
	{
		f = file_open_truncate(path);
	}
	if (f)
	{
		file_write(emu->clownmdemu.state.external_ram.buffer, emu->clownmdemu.state.external_ram.size, f);
		file_close(f);
	}
	else
	{
		printf("failed to open %s as cartridge save ram for writing\n", comb);
	}
	free(path);
	free(comb);
	free(strip);
}

void emulator_load_state(emulator * emu, const char * filename)
{
	char tmp[8];
	FILE * f;
	char * path;
	char * comb;
	char * strip;
	size_t read;
	if (!filename)
	{
		strip = strip_ext(emu->cartridge_filename ? emu->cartridge_filename : emu->cd_filename);
		comb = append_ext(strip, "state");
		path = build_file_path(get_exe_dir(), comb);
	}
	else
	{
		strip = comb = NULL;
		path = strdup(filename);
	}
	if (path)
	{
		if (file_exists(path))
		{
			if (file_size(path) != (long) save_state_size)
			{
				printf("state file size mismatch, got %ld bytes, expected %lu\n", file_size(path), save_state_size);
			}
			else
			{
				f = file_open_read(path);
				if (!f)
				{
					printf("unable to load state file %s\n", path);
				}
				else
				{
					read = file_read(tmp, sizeof(save_state_magic), f);
					if (read < sizeof(save_state_magic) || strcmp(save_state_magic, tmp) != 0)
					{
						printf("state file signature invalid\n");
					}
					else
					{
						read += file_read(&emu->state_backup, sizeof(ClownMDEmu_StateBackup), f);
						read += file_read(&emu->cd_backup, sizeof(CDReader_StateBackup), f);
						read += file_read(emu->colors_backup, sizeof(palette), f);
						if (read != save_state_size)
						{
							printf("state read error, got %lu bytes, expected %lu\n", read, save_state_size);
						}
						else
						{
							ClownMDEmu_LoadState(&emu->clownmdemu, &emu->state_backup);
							CDReader_LoadState(&emu->cd, &emu->cd_backup);
							memcpy(emu->colors, emu->colors_backup, sizeof(emu->colors_backup));
							printf("state loaded successfully from %s\n", path);
						}
					}
					file_close(f);
				}
			}
		}
		else
		{
			printf("state file %s does not exist\n", path);
		}
	}
	free(path);
	free(comb);
	free(strip);
}

void emulator_save_state(emulator * emu)
{
	FILE * f;
	char * path;
	char * comb;
	char * strip;
	size_t written;
	strip = strip_ext(emu->cartridge_filename ? emu->cartridge_filename : emu->cd_filename);
	comb = append_ext(strip, "state");
	path = build_file_path(get_exe_dir(), comb);
	if (path)
	{
		ClownMDEmu_SaveState(&emu->clownmdemu, &emu->state_backup);
		CDReader_SaveState(&emu->cd, &emu->cd_backup);
		memcpy(emu->colors_backup, emu->colors, sizeof(emu->colors));
		f = file_open_truncate(path);
		if (f)
		{
			written = file_write(save_state_magic, sizeof(save_state_magic), f);
			written += file_write(&emu->state_backup, sizeof(ClownMDEmu_StateBackup), f);
			written += file_write(&emu->cd_backup, sizeof(CDReader_StateBackup), f);
			written += file_write(&emu->colors_backup, sizeof(palette), f);
			if (written != save_state_size)
			{
				printf("state write error, got %lu bytes, expected %lu\n", written, save_state_size);
			}
			else
			{
				printf("state saved successfully to %s\n", path);
			}
			file_close(f);
		}
		else
		{
			printf("failed to save state to %s\n", path);
		}
	}
	free(path);
	free(comb);
	free(strip);
}

void emulator_shutdown_audio(emulator * emu)
{
	if (emu->audio_init)
	{
		Mixer_Deinitialise(&emu->mixer);
		emu->audio_init = cc_false;
		free(emu->samples);
		emu->samples = NULL;
	}
}

void emulator_shutdown(emulator * emu)
{
	if (emu->cd_boot)
	{
		emulator_unload_cd(emu);
	}
	CDReader_Deinitialise(&emu->cd);
	emulator_unload_cartridge(emu);
	emulator_shutdown_audio(emu);
}
