PROGS = v4l2_wayland

all: $(PROGS)

LFLAGS = $(shell pkg-config --libs wayland-client) \
				 $(shell pkg-config --libs cairo) \
				 $(shell pkg-config --libs pangocairo) \
				 -lccv -lm -lpng -ljpeg -lswscale -lavutil -lswresample \
				 -lavformat -lavcodec -lpthread -ljack
LIBS =
CFLAGS = $(shell pkg-config --cflags pangocairo)
SRCS = v4l2_wayland.c muxing.c sound_shape.c midi.c
OBJS = $(SRCS:.c=.o)
HDRS = muxing.h sound_shape.h midi.h

.SUFFIXES:

.SUFFIXES: .c

%.o : %.c ${HDRS}
	$(CC) ${CFLAGS} -c $< -o $@

v4l2_wayland: v4l2_wayland.c ${OBJS}
	${CC} -o $@ ${OBJS} ${LFLAGS}

clean:
	rm -f ${OBJS} $(PROGS) $(PROGS:%=%.o)
