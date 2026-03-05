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

#include "emulator.h"
#include "file.h"
#include "path.h"

#include <time.h>

/* CLOCK_MONOTONIC_RAW is linux-only */
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

#ifndef DISABLE_AUDIO
#if defined(__linux__)
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

#define BILLION 1000000000L
#define ROM_SIZE_MAX 0x800000
#define FRAMEBUFFER_SIZE VDP_MAX_SCANLINE_WIDTH * VDP_MAX_SCANLINES * sizeof(uint32_t)

static void usage(const char * app_name)
{
	printf(
		"Usage: %s [OPTIONS] FILE\n"
		"Options:\n"
		"        -h, -?     Print this help text\n"
		"        -r (U|J|E) Set region to US, Japan or Europe respectively\n"
		"        -l         Enable emulator core log output (disabled by default)\n"
		"        -w         Enable widescreen hack (disabled by default)\n"
		"        -s FILE    Load save state from specified file\n"
		"        -c FILE    Load specified file as a cartridge\n"
		"        -d FILE    Load specified file as a disc\n",
		app_name
	);
}

static void toggle_key(emulator * emu, int keysym, cc_bool down)
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

/* init and main loop */
int main(int argc, char ** argv)
{
	int ret;
	long ns_desired;
	
	emulator * emu;
	
	cc_bool log_enabled;
	cc_bool widescreen_enabled;
	int width;
	int height;
	int region;
	const char * filename;
	const char * cartridge_file;
	const char * cd_file;
	const char * state_file;
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
	cc_bool audio_init;
#endif
#endif
	ret = 1;
	
	if (argc < 2)
	{
		usage(argv[0]);
		return ret;
	}
	
	log_enabled = cc_false;
	widescreen_enabled = cc_false;
	region = REGION_UNSPECIFIED;
	filename = NULL;
	cartridge_file = NULL;
	cd_file = NULL;
	state_file = NULL;
	
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
				return ret;
			}
			
			switch (argv[i][1])
			{
				case 'h':
				case '?':
					usage(argv[0]);
					return ret;
				case 'r':
					if (i == argc - 1)
					{
						printf("region not specified\n");
						return ret;
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
								return ret;
						}
					}
					break;
				case 'l':
					log_enabled = cc_true;
					break;
				case 'w':
					widescreen_enabled = cc_true;
					break;
				case 's':
					if (i == argc - 1)
					{
						printf("state file not specified\n");
						return ret;
					}
					else
					{
						i++;
						if (!state_file)
						{
							state_file = argv[i];
						}
						else
						{
							printf("specify only 1 state file\n");
							return ret;
						}
					}
					break;
				case 'c':
					if (i == argc - 1)
					{
						printf("cartridge file not specified\n");
						return ret;
					}
					else
					{
						i++;
						if (!cartridge_file)
						{
							cartridge_file = argv[i];
						}
						else
						{
							printf("specify only 1 cartridge file\n");
							return ret;
						}
					}
					break;
				case 'd':
					if (i == argc - 1)
					{
						printf("cd file not specified\n");
						return ret;
					}
					else
					{
						i++;
						if (!cd_file)
						{
							cd_file = argv[i];
						}
						else
						{
							printf("specify only 1 cd file\n");
							return ret;
						}
					}
					break;
				default:
					printf("unknown flag %s\n", argv[i]);
					usage(argv[0]);
					return ret;
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
				return ret;
			}
		}
	}
	
	if (!filename && !cartridge_file && !cd_file)
	{
		printf("no bootable media filename specified\n");
		return ret;
	}
	
	if (!exe_dir_init(argv[0]))
	{
		warn("unable to get executable directory, saves will not be available!\n");
	}
	
	width = widescreen_enabled == cc_true ? VDP_MAX_SCANLINE_WIDTH : VDP_H40_SCREEN_WIDTH_IN_TILE_PAIRS * VDP_TILE_PAIR_WIDTH;
	height = VDP_MAX_SCANLINES;
	
	emu = (emulator *) malloc(sizeof(emulator));
	if (!emu)
	{
		printf("unable to alloc emu\n");
		return ret;
	}
	memset(emu, 0, sizeof(emulator));
	
	emu->framebuffer = (uint32_t *) malloc(FRAMEBUFFER_SIZE);
	if (!emu->framebuffer)
	{
		printf("unable to alloc internal framebuffer\n");
		goto cleanup_emu;
	}
	
	/* init window */
	bit_depth = 24;
	attr_mask = CWBackPixel | CWColormap | CWEventMask;
	display = XOpenDisplay(0);
	if (!display)
	{
		printf("unable to open display\n");
		goto cleanup_emu;
	}
	root = DefaultRootWindow(display);
	default_screen = DefaultScreen(display);
	if (!XMatchVisualInfo(display, default_screen, bit_depth, TrueColor, &vis_info))
	{
		printf("no matching visual info\n");
		goto cleanup_x11_display;
	}
	window_attr.background_pixel = 0;
	window_attr.colormap = XCreateColormap(display, root, vis_info.visual, AllocNone);
	window_attr.event_mask = KeyPressMask | KeyReleaseMask;
	window = XCreateWindow(display, root, 0, 0, width, height, 0, vis_info.depth, InputOutput, vis_info.visual, attr_mask, &window_attr);
	if (!window)
	{
		printf("unable to create window\n");
		goto cleanup_x11_display;
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
		goto cleanup_x11_display;
	}
	default_gc = DefaultGC(display, default_screen);
	wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
	if (!XSetWMProtocols(display, window, &wm_delete_window, 1))
	{
		printf("unable to intercept window close event\n");
		goto cleanup_x11_window;
	}
	
	/* init emu */
	ClownMDEmu_Constant_Initialise();
	emulator_init(emu);
	emulator_set_options(emu, log_enabled, widescreen_enabled);
	if (cartridge_file)
	{
		if (!emulator_load_cartridge(emu, cartridge_file))
		{
			printf("unable to load cartridge\n");
			goto cleanup_x11_window;
		}
	}
	if (cd_file)
	{
		if (!emulator_load_cd(emu, cd_file))
		{
			printf("unable to load cd\n");
			goto cleanup_x11_window;
		}
	}
	if (!cartridge_file && !cd_file)
	{
		if (!emulator_load_file(emu, filename))
		{
			printf("unable to load file\n");
			goto cleanup_x11_window;
		}
	}
	emulator_set_region(emu, region);
	emulator_init_audio(emu);
	
#ifndef DISABLE_AUDIO
	/* init audio */
#if defined(__linux__)
	audio_params.format = PA_SAMPLE_S16LE;
	audio_params.channels = MIXER_CHANNEL_COUNT;
	audio_params.rate = emu->clownmdemu.configuration.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL ? MIXER_OUTPUT_SAMPLE_RATE_PAL : MIXER_OUTPUT_SAMPLE_RATE_NTSC;
	audio_device = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK, NULL, "audio", &audio_params, NULL, NULL, &audio_error);
	if (!audio_device)
	{
		warn("unable to create audio device: %s\n", pa_strerror(audio_error));
	}
#elif defined(__OpenBSD__)
	audio_init = cc_false;
	audio_device = sio_open(SIO_DEVANY, SIO_PLAY, 0);
	if (!audio_device)
	{
		warn("unable to open audio device\n");
		goto skip_audio_init;
	}
	sio_initpar(&audio_params);
	audio_params.bits = 16;
	audio_params.bps = SIO_BPS(16);
	audio_params.le = SIO_LE_NATIVE;
	audio_params.pchan = MIXER_CHANNEL_COUNT;
	audio_params.rate = emu->clownmdemu.configuration.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL ? MIXER_OUTPUT_SAMPLE_RATE_PAL : MIXER_OUTPUT_SAMPLE_RATE_NTSC;
	audio_params.xrun = SIO_IGNORE;
	if (!sio_setpar(audio_device, &audio_params))
	{
		warn("unable to set audio properties\n");
		goto skip_audio_init;
	}
	if (!sio_start(audio_device))
	{
		warn("unable to start audio device\n");
	}
	else
	{
		audio_init = cc_true;
	}
skip_audio_init:
#endif
#endif
	
	emulator_reset(emu, cc_true);
	if (state_file)
	{
		emulator_load_state(emu, state_file);
	}
	
	ns_desired = BILLION / (emu->clownmdemu.configuration.tv_standard == CLOWNMDEMU_TV_STANDARD_NTSC ? 60.0f : 50.0f);
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
					switch (keysym)
					{
						case XK_Escape:
							running = 0;
							break;
						case XK_Tab:
							emulator_reset(emu, cc_false);
							break;
						default:
							toggle_key(emu, keysym, cc_true);
							break;
					}
					break;
				case KeyRelease:
					ek = (XKeyPressedEvent *) &ev;
					keysym = XkbKeycodeToKeysym(display, ek->keycode, 0, 0);
					switch (keysym)
					{
						case XK_F5:
							emulator_save_state(emu);
							break;
						case XK_F8:
							emulator_load_state(emu, NULL);
							break;
						default:
							toggle_key(emu, keysym, cc_false);
							break;
					}
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
		if (audio_device)
		{
			pa_simple_write(audio_device, emu->samples, emu->audio_bytes, &audio_error);
		}
#elif defined(__OpenBSD__)
		if (audio_init)
		{
			sio_write(audio_device, emu->samples, emu->audio_bytes);
		}
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
	ret = 0;
#ifndef DISABLE_AUDIO
#if defined(__linux__)
	if (audio_device)
	{
		pa_simple_drain(audio_device, &audio_error);
		pa_simple_free(audio_device);
	}
#elif defined(__OpenBSD__)
	if (audio_init)
	{
		sio_stop(audio_device);
		sio_close(audio_device);
	}
#endif
#endif
cleanup_x11_window:
	XDestroyWindow(display, window);
	XDestroyImage(x_window_buffer);
cleanup_x11_display:
	XCloseDisplay(display);
cleanup_emu:
	emulator_shutdown(emu);
	free(emu);
	return ret;
}
