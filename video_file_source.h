#if !defined(_VIDEO_FILE_SOURCE_H)
#define _VIDEO_FILE_SOURCE_H (1)

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <jack/ringbuffer.h>
#include <libswscale/swscale.h>
#include <pthread.h>

#include "draggable.h"

typedef struct video_file {
	draggable dr;
	uint8_t allocated;
	uint8_t active;
	char name[256];
	AVFormatContext *fmt_ctx;
	AVCodecContext *video_dec_ctx;
  AVStream *video_stream;
  struct SwsContext *resample;
	int width;
	int height;
	enum AVPixelFormat pix_fmt;
	int video_stream_idx;
	uint8_t *video_dst_data[4];
	int video_dst_linesize[4];
	int video_dst_bufsize;
	AVFrame *frame;
	AVFrame *decoded_frame;
	AVPacket pkt;
  pthread_t thread_id;
  pthread_mutex_t lock;
  pthread_cond_t data_ready;
	int playing;
	int decoding_started;
	int decoding_finished;
	double total_playtime;
	double current_playtime;
	struct timespec play_start_ts;
	jack_ringbuffer_t *vbuf;
} video_file_t;

void video_file_create(video_file_t *vf, char *name, uint64_t z);
int video_file_in(video_file_t *vf, double x, double y);
int video_file_destroy(video_file_t *vf);
int video_file_play(video_file_t *vf);
void *video_file_thread(void *arg);
#endif

