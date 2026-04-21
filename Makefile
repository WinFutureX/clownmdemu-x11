.SUFFIXES: .c .o
.PHONY: all clean

DEBUG ?= 0
DISABLE_AUDIO ?= 0
STRICT ?= 0
ASAN ?= 0

OS := $(shell uname -s)

X11_CFLAGS := $(shell pkg-config x11 --cflags)
X11_LDFLAGS := $(shell pkg-config x11 --libs)

ifeq ($(DISABLE_AUDIO), $(filter $(DISABLE_AUDIO), 1 Y y))
AUDIO_CFLAGS := -DDISABLE_AUDIO
else
ifeq ($(OS), Linux)
AUDIO_CFLAGS := $(shell pkg-config libpulse-simple --cflags)
AUDIO_LDFLAGS := $(shell pkg-config libpulse-simple --libs) -lrt
else ifeq ($(OS), OpenBSD)
AUDIO_LDFLAGS := -lsndio
endif
endif

ifeq ($(DEBUG), $(filter $(DEBUG), 1 Y y))
OPT_CFLAGS := -g3 -O0
else
OPT_CFLAGS := -O2
endif

CFLAGS := -std=gnu89 $(OPT_CFLAGS) $(X11_CFLAGS) $(AUDIO_CFLAGS)
LDFLAGS := -lm $(X11_LDFLAGS) $(AUDIO_LDFLAGS)

GIT_INFO := $(shell git rev-parse 2> /dev/null; echo $$?)
ifeq ($(GIT_INFO), 0)
CFLAGS += -DGIT_COMMIT_HASH_ROOT=\"$(shell git rev-parse HEAD)\" -DGIT_COMMIT_HASH_COMMON=\"$(shell git -C common rev-parse HEAD)\"
endif

ifeq ($(STRICT), $(filter $(STRICT), 1 Y y))
CFLAGS += -Wall -Wextra
endif

ifeq ($(ASAN), $(filter $(ASAN), 1 Y y))
CFLAGS += -fsanitize=address
endif

OBJS = common.o emulator.o file.o main.o path.o

all: clownmdemu

clownmdemu: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) clownmdemu
