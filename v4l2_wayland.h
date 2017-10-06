#if !defined(_V4L2_WAYLAND_H)
#define _V4L2_WAYLAND_H (1)

#include <pthread.h>
#include <libavformat/avformat.h>
#include <jack/ringbuffer.h>

void *snapshot_disk_thread(void *);

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

int timespec2file_name(char *buf, uint len, char *dir, char *extension,
 struct timespec *ts);
#endif
