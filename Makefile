CC = cc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -D_DEFAULT_SOURCE -I. $(SDL_CFLAGS)
LDLIBS = $(SDL_LIBS) -lpthread

OBJS = common.o \
       i8051/cpu.o i8051/op.o i8051/peripheral.o \
       machine/generic.o machine/duart.o machine/vt420.o machine/video.o machine/vt5xx.o \
       lk201/lk201.o lk201/keys.o \
       ssu/chan.o ssu/session.o ssu/xonoff.o ssu/config.o \
       host/comm.o host/logging.o host/unicode.o host/headless.o \
       host/fb_render.o host/sdl.o host/text.o host/termkey.o

HDRS = common.h i8051/i8051.h machine/machine.h lk201/lk201.h ssu/ssu.h host/host.h

all: ezalb

ezalb: $(OBJS) main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

boot_test: $(OBJS) tests/boot_test.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: boot_test
	./boot_test roms/vt420/23-068E9-00.bin

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) main.o tests/boot_test.o ezalb boot_test

.PHONY: all test clean
