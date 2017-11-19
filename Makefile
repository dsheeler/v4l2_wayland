CC=$(CXX)
PREFIX=/usr
APPDIR=$(PREFIX)/share/applications
BINDIR=$(PREFIX)/bin
PROGNAME=v4l2_wayland
DESKTOP_FILENAME=$(PROGNAME).desktop
ICON=v4l2_wayland.svg
ICONDIR=$(PREFIX)/share/icons/hicolor/scalable/apps
PROGS=$(PROGNAME)

all: $(PROGS)

LFLAGS = $(shell pkg-config --libs cairo) \
				 $(shell pkg-config --libs pangocairo) \
				 $(shell pkg-config --libs gtk+-3.0) \
				 $(shell pkg-config --libs fftw3) \
				 -lccv -lm -lpng -ljpeg -lswscale -lavutil -lswresample \
				 -lavformat -lavcodec -lpthread -ljack
LIBS =
CFLAGS = -O3 -ffast-math -Wall
#CFLAGS =-g -Wall
CFLAGS +=	$(shell pkg-config --cflags pangocairo) \
				 	$(shell pkg-config --cflags gtk+-3.0) \
				 	$(shell pkg-config --cflags fftw3)
SRCS = draggable.c v4l2_wayland.c muxing.c sound_shape.c midi.c kmeter.c \
			 video_file_source.c dingle_dots.c v4l2.c
OBJS = $(SRCS:.c=.o)
HDRS = draggable.h muxing.h sound_shape.h midi.h v4l2_wayland.h kmeter.h \
			video_file_source.h dingle_dots.h v4l2.h

.SUFFIXES:

.SUFFIXES: .c

%.o : %.c ${HDRS}
	$(CC) ${CFLAGS} -c $< -o $@

v4l2_wayland: v4l2_wayland.c ${OBJS}
	${CC} -o $@ ${OBJS} ${LFLAGS}

install: all
	install $(PROGNAME) $(BINDIR)
	install -m 644 $(DESKTOP_FILENAME) $(APPDIR)
	install -m 644 $(ICON) $(ICONDIR)

uninstall:
	rm -f $(BINDIR)/$(PROGNAME)
	rm -f $(APPDIR)/$(DESKTOP_FILENAME)
	rm -f $(ICONDIR)/$(ICON)

clean:
	rm -f ${OBJS} $(PROGS) $(PROGS:%=%.o)
