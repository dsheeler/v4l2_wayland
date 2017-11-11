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
#include "draggable.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define CLIPVALUE(v) ((v) < 255 ? (v) : 255)

#define NEVENTS 1
#define DD_V4L2_MAX_STR_LEN 256

struct dd_v4l2_buffer {
  void   *start;
  size_t  length;
};

class V4l2 : Draggable {
	public:
		V4l2();
		int read_frames();
		int in(double x, double y);
		void stop_capturing();
		void start_capturing();
		void uninit_device();
		void init_device();
		void close_device();
		void open_device();
		void init_mmap();
	private:
		static int xioctl(int fh, int request, void *arg);
		static void YUV2RGB(const unsigned char y, const unsigned char u,
		 const unsigned char v, unsigned char* r, unsigned char* g,
		 unsigned char* b);
		char dev_name[DD_V4L2_MAX_STR_LEN];
		int fd;
		int active;
		int width;
		int height;
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

#endif