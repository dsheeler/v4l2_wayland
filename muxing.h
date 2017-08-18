#if !defined(_MUXING_H)
#define _MUXING_H (1)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "v4l2_wayland.h"
#include "dingle_dots.h"

#define STREAM_FRAME_RATE 120 /* 25 images/s */
#define STREAM_PIX_FMT    AV_PIX_FMT_RGB32 /* default pix_fmt */

#define SCALE_FLAGS SWS_BICUBIC

int write_video_frame(AVFormatContext *oc, OutputStream *ost);
int write_audio_frame(dingle_dots_t *dd, AVFormatContext *oc,
 OutputStream *ost);
int init_output();
void close_stream(AVFormatContext *oc, OutputStream *ost);
extern AVFormatContext *oc;
extern AVFrame *frame;
extern jack_ringbuffer_t *video_ring_buf, *audio_ring_buf;
extern char *out_file_name;
extern uint32_t stream_bitrate;
extern double ascale_factor_x;
extern double ascale_factor_y;
extern uint32_t width;
extern uint32_t height;
extern uint32_t awidth;
extern uint32_t aheight;
extern volatile int can_capture;
extern int recording_started;
extern int recording_stopped;
extern pthread_mutex_t av_thread_lock;
#endif
