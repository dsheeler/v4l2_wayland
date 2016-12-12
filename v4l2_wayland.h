#if !defined(_V4L2_WAYLAND_H)
#define _V4L2_WAYLAND_H (1)

#include <pthread.h>

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
  struct timespec last_time;
  int samples_count;
  int64_t overruns;
  output_frame out_frame;
  AVFrame *frame;
  AVFrame *tmp_frame;
  float t, tincr, tincr2;
  struct SwsContext *sws_ctx;
  struct SwrContext *swr_ctx;
} OutputStream;

typedef struct disk_thread_info {
  pthread_t thread_id;
  pthread_mutex_t lock;
  pthread_cond_t data_ready;
  OutputStream *stream;
} disk_thread_info_t;

typedef struct {
  disk_thread_info_t *audio_thread_info;
  disk_thread_info_t *video_thread_info;
} dingle_dots_t;

#endif
