#if !defined (_V4L2_H)
#define _V4L2_H (1)

#include <poll.h>
#include <assert.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <jack/ringbuffer.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include "v4l2_wayland.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define CLIPVALUE(v) ((v) < 255 ? (v) : 255)

#define NEVENTS 1
#define DD_V4L2_MAX_STR_LEN 256

typedef struct dd_v4l2_t dd_v4l2_t;

struct dd_v4l2_buffer {
  void   *start;
  size_t  length;
};

struct dd_v4l2_t {
	char dev_name[DD_V4L2_MAX_STR_LEN];
	int fd;
	GdkRectangle rect;
	struct dd_v4l2_buffer *buffers;
	unsigned int n_buffers;
  uint32_t *save_buf;
	uint32_t *read_buf;
	struct pollfd pfd[1];
	pthread_t thread_id;
  pthread_mutex_t lock;
  pthread_cond_t data_ready;
	jack_ringbuffer_t *rbuf;
};

int dd_v4l2_read_frames(dd_v4l2_t *v);
void dd_v4l2_stop_capturing(dd_v4l2_t *v);
void dd_v4l2_start_capturing(dd_v4l2_t *v);
void dd_v4l2_uninit_device(dd_v4l2_t *v);
void dd_v4l2_init_device(dd_v4l2_t *v);
void dd_v4l2_close_device(dd_v4l2_t *v);
void dd_v4l2_open_device(dd_v4l2_t *v);
#endif
