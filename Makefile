PROGS = v4l2_wayland

all: $(PROGS)

LFLAGS = $(shell pkg-config --libs wayland-client) \
				 $(shell pkg-config --libs wayland-cursor) \
				 $(shell pkg-config --libs cairo) \
				 $(shell pkg-config --libs pangocairo) \
				 -lccv -lm -lpng -ljpeg -lswscale -lavutil -lswresample \
				 -lavformat -lavcodec -lpthread -ljack
LIBS =
CFLAGS = -Wall -g $(shell pkg-config --cflags pangocairo)
SRCS = v4l2_wayland.c muxing.c sound_shape.c midi.c kmeter.c
OBJS = $(SRCS:.c=.o)
HDRS = muxing.h sound_shape.h midi.h v4l2_wayland.h kmeter.h

.SUFFIXES:

.SUFFIXES: .c

%.o : %.c ${HDRS}
	$(CC) ${CFLAGS} -c $< -o $@

v4l2_wayland: v4l2_wayland.c ${OBJS}
	${CC} -o $@ ${OBJS} ${LFLAGS}

clean:
	rm -f ${OBJS} $(PROGS) $(PROGS:%=%.o)
