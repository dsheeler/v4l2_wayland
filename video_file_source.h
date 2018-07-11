#if !defined(_VIDEO_FILE_SOURCE_H)
#define _VIDEO_FILE_SOURCE_H (1)

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
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
#include "vwdrawable.h"

class VideoFile : public vwDrawable {
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
	char name[256];
	AVFormatContext *fmt_ctx;
	AVCodecContext *video_dec_ctx;
	AVCodecContext *audio_dec_ctx;
	AVStream *video_stream;
	AVStream *audio_stream;
	struct SwsContext *video_resample;
	struct SwrContext *audio_resample;
	enum AVPixelFormat pix_fmt;
	int video_stream_idx;
	int audio_stream_idx;
	uint8_t *video_dst_data[4];
	int video_dst_linesize[4];
	int video_dst_bufsize;
	AVFrame *video_frame;
	AVFrame *decoded_video_frame;
	AVFrame *audio_frame;
	AVPacket pkt;
	pthread_t thread_id;
	pthread_mutex_t video_lock;
	pthread_cond_t video_data_ready;
	pthread_mutex_t audio_lock;
	pthread_cond_t audio_data_ready;
	pthread_mutex_t pause_lock;
	pthread_cond_t pause_unpuase;
	int playing;
	int paused;
	int audio_playing;
	int video_decoding_started;
	int video_decoding_finished;
	int audio_decoding_started;
	int audio_decoding_finished;
	uint8_t have_audio;
	uint64_t nb_frames_played;
	double total_playtime;
	double current_playtime;
	struct timespec play_start_ts;
	jack_ringbuffer_t *vbuf;
	jack_ringbuffer_t *abuf;
	void toggle_play_pause();
};

#endif

