.SUFFIXES: .c .o

OS != uname -s

X11_CFLAGS != pkg-config x11 --cflags
X11_LDFLAGS != pkg-config x11 --libs

ifeq ($(OS), Linux)
AUDIO_CFLAGS != pkg-config libpulse-simple --cflags
AUDIO_LDFLAGS != pkg-config libpulse-simple --libs
else ifeq ($(OS), OpenBSD)
AUDIO_LDFLAGS := -lsndio
endif

CFLAGS := -std=c89 -pedantic -g3 -O0 $(X11_CFLAGS) $(AUDIO_CFLAGS)
LDFLAGS := -lm $(X11_LDFLAGS) $(AUDIO_LDFLAGS)

OBJS = main.o common.o

clownmdemu: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o clownmdemu
