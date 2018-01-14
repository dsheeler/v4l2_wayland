#if !defined(_VIDEO_FILE_SOURCE_H)
#define _VIDEO_FILE_SOURCE_H (1)

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#ifdef __cplusplus
}
#endif
#include <jack/ringbuffer.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <vector>

#include "v4l2_wayland.h"
#include "drawable.h"

class VideoFile : public Drawable {
public:
	VideoFile();
	int create(char *name, double x, double y, uint64_t z);
	int destroy();
	bool render(std::vector<cairo_t *> &contexts);
	int play();
	static void *thread(void *arg);
	/*private:*/
	static int open_codec_context(int *stream_idx, AVCodecContext **dec_ctx,
								  AVFormatContext *fmt_ctx, enum AVMediaType type);
	DingleDots *dd;

	char name[256];
	AVFormatContext *fmt_ctx;
	AVCodecContext *video_dec_ctx;
	AVStream *video_stream;
	struct SwsContext *resample;
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
};

#endif

