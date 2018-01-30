#if !defined(_V4L2_WAYLAND_H)
#define _V4L2_WAYLAND_H (1)

#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <ccv/ccv.h>
#ifdef __cplusplus
}
#endif
#include <jack/ringbuffer.h>
#include <cairo.h>

#define vw_min(a, b) ((a) < (b) ? (a) : (b))
#define vw_max(a, b) ((a) > (b) ? (a) : (b))

#define FFT_SIZE 256
#define MIDI_RB_SIZE 1024 * sizeof(struct midi_message)


void *snapshot_disk_thread(void *);
void *dd_v4l2_thread(void *);

typedef struct color {
	double r;
	double g;
	double b;
	double a;
} color;

struct hsva {
	double h;
	double s;
	double v;
	double a;
};

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
int timespec2file_name(char *buf, uint len, const char *dir,
					   const char *extension,
					   struct timespec *ts);
void errno_exit(const char *s);
#endif
