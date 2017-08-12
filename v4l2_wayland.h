#if !defined(_V4L2_WAYLAND_H)
#define _V4L2_WAYLAND_H (1)

#include <pthread.h>
#include <gtk/gtk.h>
#include "sound_shape.h"
#include "kmeter.h"

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
  OutputStream *stream;
} disk_thread_info_t;

typedef struct {
  GApplication *app;
  disk_thread_info_t *audio_thread_info;
  disk_thread_info_t *video_thread_info;
  sound_shape sound_shapes[MAX_NSOUND_SHAPES];
  kmeter meters[2];
	int doing_motion;
	int doing_tld;
	float motion_threshold;
	cairo_surface_t *csurface;
  cairo_t *cr;
} dingle_dots_t;

int midi_scale_init(midi_scale_t *scale, uint8_t *notes, uint8_t nb_notes);
int midi_key_init(midi_key_t *key, uint8_t base_note, midi_scale_t *scale);
int dingle_dots_init(dingle_dots_t *dd, midi_key_t *keys, uint8_t nb_keys);
#endif
