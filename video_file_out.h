#ifndef VIDEO_FILE_OUT_H
#define VIDEO_FILE_OUT_H

#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

#include <jack/ringbuffer.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include "v4l2_wayland.h"

#define STREAM_FRAME_RATE 60
#define STREAM_PIX_FMT AV_PIX_FMT_RGB32
#define VF_STR_LEN 256
#define SCALE_FLAGS SWS_BICUBIC

class VideoFileOut
{
public:
	VideoFileOut();
	int allocate_audio();
	int write_video_frame();
	int write_audio_frame();
	int get_recording_started();
	int get_recording_stopped();
	void start_recording(int width, int height, int bitrate);
	void stop_recording();
	void set_can_capture(int val);
	pthread_mutex_t *get_video_lock();
	pthread_cond_t *get_video_data_ready();
	void set_video_first_time(struct timespec val);
	AVFormatContext *get_video_output_context();
	int get_video_first_call();
	void set_video_first_call(int val);
	int get_video_done();
	void set_video_done(int val);
	jack_ringbuffer_t *get_video_ringbuffer();
	void set_audio_first_time(struct timespec val);
	void set_audio_samples_count(int val);
	int get_audio_first_call();
	void set_audio_first_call(int val);
	int get_audio_done();
	void set_audio_done(int val);
	int get_trailer_written();
	void set_trailer_written(int val);
	void write_trailer();
	pthread_mutex_t *get_av_thread_lock();
	jack_ringbuffer_t *get_audio_ringbuffer();
	pthread_mutex_t *get_audio_lock();
	pthread_cond_t *get_audio_data_ready();
	void set_audio_ringbuffer(jack_ringbuffer_t *val);

	int get_audio_frame(AVFrame **ret_frame);
	int get_video_frame(AVFrame **ret_frame);
	int init_output();
	disk_thread_info_t *get_video_thread_info();
	struct timespec *get_audio_first_time();
	int allocate_video();
	void deallocate_audio();
	void deallocate_video();
	int get_recording_audio() const;
	void set_recording_audio(int value);
	void wake_up_audio_write_thread();

	private:
	int recording_audio;
	int recording_video;
	struct timespec video_first_time;
	static void *audio_thread(void *);
	static void *video_thread(void *);
	void close_stream();
	jack_ringbuffer_t *video_ring_buf;
	jack_ringbuffer_t *audio_ring_buf;
	const size_t sample_size = sizeof(jack_default_audio_sample_t);
	pthread_mutex_t	av_thread_lock = PTHREAD_MUTEX_INITIALIZER;
	char name[VF_STR_LEN];
	int recording_started;
	int recording_stopped;
	int audio_done;
	int video_done;
	int trailer_written;
	uint32_t bitrate;
	int width;
	int	height;
	AVFormatContext *video_output_context;
	struct timespec out_frame_ts;
	disk_thread_info_t audio_thread_info;
	disk_thread_info_t video_thread_info;
	OutputStream video_st;
	int can_capture;
	int first_call_audio = 1;
	int first_call_video = 1;


};

#endif // VIDEO_FILE_OUT_H
