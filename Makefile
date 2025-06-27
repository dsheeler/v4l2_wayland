CC=$(CXX)
PREFIX=/usr
APPDIR=$(PREFIX)/share/applications
BINDIR=$(PREFIX)/bin
PROGNAME=v4l2_wayland
DESKTOP_FILENAME=$(PROGNAME).desktop
ICON=v4l2_wayland.svg
ICONDIR=$(PREFIX)/share/icons/hicolor/scalable/apps
PROGS=$(PROGNAME) vw_client

all: $(PROGS)

LFLAGS = $(shell pkg-config --libs cairo) \
				 $(shell pkg-config --libs pangocairo) \
				 $(shell pkg-config --libs gtk+-3.0) \
				 $(shell pkg-config --libs gtkmm-3.0) \
				 $(shell pkg-config --libs fftw3) \
				 $(shell pkg-config --libs glib-2.0 libcanberra) \
				 $(shell pkg-config --libs opencv4) \
				 -lccv -lm -lpng -ljpeg -lswscale -lavutil -lswresample \
				 -lavformat -lavcodec -lpthread -ljack -lXext -lX11 -lXfixes
LIBS =
#CFLAGS = -O3 -ffast-math -Wall
CFLAGS =-g -Wall
CFLAGS +=	$(shell pkg-config --cflags pangocairo) \
				 	$(shell pkg-config --cflags gtk+-3.0) \
				 	$(shell pkg-config --cflags sigc++-2.0) \
				 	$(shell pkg-config --cflags gtkmm-3.0) \
					$(shell pkg-config --cflags fftw3) \
					$(shell pkg-config --cflags glib-2.0 libcanberra) \
					$(shell pkg-config --cflags opencv4) 

SRCS = vwdrawable.cc v4l2_wayland.cc sound_shape.cc midi.cc kmeter.cc \
			 video_file_source.cc dingle_dots.cc v4l2.cc sprite.cc snapshot_shape.cc \
			 easer.cc easable.cc video_file_out.cc x11.cc text.cc \
			 vwcolor.cc server.cc hex.cc opencvCam.cc

CSRCS= easing.c

OBJS := $(SRCS:.cc=.o) $(CSRCS:.c=.o)

HDRS = vwdrawable.h sound_shape.h midi.h v4l2_wayland.h kmeter.h \
			video_file_source.h dingle_dots.h v4l2.h sprite.h snapshot_shape.h \
			easer.h easing.h easable.h video_file_out.h x11.h text.h \
			vwcolor.h server.h hex.h opencvCam.h

.SUFFIXES:

.SUFFIXES: .cc

%.o : %.cc ${HDRS}
	$(CC) ${CFLAGS} -c $< -o $@

%.o : %.c ${HDRS}
	$(CC) ${CFLAGS} -c $< -o $@

v4l2_wayland: v4l2_wayland.cc ${OBJS}
	${CC} -o $@ ${OBJS} ${LFLAGS}

vw_client: vw_client.cc
	${CC} -o $@ vw_client.cc -lreadline

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
