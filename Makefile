PROGS = v4l2_wayland

all: $(PROGS)

LFLAGS = $(shell pkg-config --libs wayland-client) \
				 $(shell pkg-config --libs cairo) \
				 -lccv -lm -lpng -ljpeg


LIBS =

SRCS = v4l2_wayland.c
OBJS = $(SRCS:.c=.o)
HDRS =

.SUFFIXES:

.SUFFIXES: .c

%.o : %.c
	$(CC) ${CFLAGS} -c $< -o $@

v4l2_wayland: v4l2_wayland.c ${OBJS}
	${CC} -o $@ ${OBJS} ${LFLAGS}

clean:
	rm -f ${OBJS} $(PROGS:%=%.o)
