PREFIX=/usr
APPDIR=$(PREFIX)/share/applications
BINDIR=$(PREFIX)/bin
PROGNAME=v4l2_wayland
DESKTOP_FILENAME=$(PROGNAME).desktop
PROGS=$(PROGNAME)

all: $(PROGS)

LFLAGS = $(shell pkg-config --libs wayland-client) \
				 $(shell pkg-config --libs wayland-cursor) \
				 $(shell pkg-config --libs cairo) \
				 $(shell pkg-config --libs pangocairo) \
				 $(shell pkg-config --libs gtk+-3.0) \
				 $(shell pkg-config --libs fftw3) \
				 -lccv -lm -lpng -ljpeg -lswscale -lavutil -lswresample \
				 -lavformat -lavcodec -lpthread -ljack
LIBS =
CFLAGS = -O3 -ffast-math -Wall \
				 $(shell pkg-config --cflags pangocairo) \
				 $(shell pkg-config --cflags gtk+-3.0) \
				 $(shell pkg-config --cflags fftw3)
SRCS = v4l2_wayland.c muxing.c sound_shape.c midi.c kmeter.c
OBJS = $(SRCS:.c=.o)
HDRS = muxing.h sound_shape.h midi.h v4l2_wayland.h kmeter.h

.SUFFIXES:

.SUFFIXES: .c

%.o : %.c ${HDRS}
	$(CC) ${CFLAGS} -c $< -o $@

v4l2_wayland: v4l2_wayland.c ${OBJS}
	${CC} -o $@ ${OBJS} ${LFLAGS}

install: all
	install $(PROGNAME) $(BINDIR)
	install -m 644 $(DESKTOP_FILENAME) $(APPDIR)

uninstall:
	rm -f $(BINDIR)/$(PROGNAME)
	rm -f $(APPDIR)/$(DESKTOP_FILENAME)

clean:
	rm -f ${OBJS} $(PROGS) $(PROGS:%=%.o)
