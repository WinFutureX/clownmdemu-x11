.SUFFIXES: .c .o

DEBUG ?= 0
DISABLE_AUDIO ?= 0

OS != uname -s

X11_CFLAGS != pkg-config x11 --cflags
X11_LDFLAGS != pkg-config x11 --libs

ifeq ($(DISABLE_AUDIO), $(filter $(DISABLE_AUDIO), 1 y))
AUDIO_CFLAGS := -DDISABLE_AUDIO
else
ifeq ($(OS), Linux)
AUDIO_CFLAGS != pkg-config libpulse-simple --cflags
AUDIO_LDFLAGS != pkg-config libpulse-simple --libs
else ifeq ($(OS), OpenBSD)
AUDIO_LDFLAGS := -lsndio
endif
endif

ifeq ($(DEBUG), $(filter $(DEBUG), 1 y))
OPT_CFLAGS := -g3 -O0
else
OPT_CFLAGS := -O2
endif

CFLAGS := -std=c89 -pedantic $(OPT_CFLAGS) $(X11_CFLAGS) $(AUDIO_CFLAGS)
LDFLAGS := -lm $(X11_LDFLAGS) $(AUDIO_LDFLAGS)

OBJS = main.o common.o

clownmdemu: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o clownmdemu
