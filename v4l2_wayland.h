#if !defined(_V4L2_WAYLAND_H)
#define _V4L2_WAYLAND_H (1)

#include <pthread.h>
#include <libavformat/avformat.h>
#include <jack/ringbuffer.h>
#include <cairo.h>

#define MAX_NUM_VIDEO_FILES 2

void *snapshot_disk_thread(void *);
void *dd_v4l2_thread(void *);

typedef struct pair_int_int {
	uint64_t z;
	int index;
} pair_int_int;

typedef struct output_frame {
  uint32_t *data;
  uint32_t size;
  struct timespec ts;
} output_frame;

typedef struct OutputStream {
  AVStream *st;
  AVCodecContext *enc;
  int64_t next_pts;
  struct timespec first_time;
  int samples_count;
  int64_t overruns;
  output_frame out_frame;
  AVFrame *frame;
  AVFrame *tmp_frame;
  struct SwsContext *sws_ctx;
  struct SwrContext *swr_ctx;
} OutputStream;

typedef struct disk_thread_info {
  pthread_t thread_id;
  pthread_mutex_t lock;
  pthread_cond_t data_ready;
  jack_ringbuffer_t *ring_buf;
	OutputStream stream;
} disk_thread_info_t;

void timespec_diff(struct timespec *start, struct timespec *stop,
 struct timespec *result);
double timespec_to_seconds(struct timespec *ts);
int timespec2file_name(char *buf, uint len, char *dir, char *extension,
 struct timespec *ts);
void process_image(cairo_t *cr, void *arg);
void errno_exit(const char *s);
#endif
