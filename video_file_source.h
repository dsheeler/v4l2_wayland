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

#include "draggable.h"

class VideoFile : Draggable {
	public:
		VideoFile(char *name);
		int in(double x, double y);
		int destroy();
		int play();
		static void *thread(void *arg);
	/*private:*/
		static int open_codec_context(int *stream_idx, AVCodecContext **dec_ctx,
 		 AVFormatContext *fmt_ctx, enum AVMediaType type);
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
};

#endif

