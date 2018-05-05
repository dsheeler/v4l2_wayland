#include "video_file_out.h"

VideoFileOut::VideoFileOut() {}

int VideoFileOut::allocate_audio() {
	pthread_mutex_init(&audio_thread_info.lock, NULL);
	pthread_cond_init(&audio_thread_info.data_ready, NULL);
	audio_ring_buf = jack_ringbuffer_create (2 *
											 sizeof(jack_default_audio_sample_t) *
											 96000);
	memset(audio_ring_buf->buf, 0, audio_ring_buf->size);
	return 0;
}

int VideoFileOut::allocate_video() {
	pthread_mutex_init(&video_thread_info.lock, NULL);
	pthread_cond_init(&video_thread_info.data_ready, NULL);
	uint32_t rb_size = 5 * 4 * width * height;
	video_ring_buf = jack_ringbuffer_create(rb_size);
	memset(video_ring_buf->buf, 0, video_ring_buf->size);
	return 0;
}

void VideoFileOut::deallocate_audio() {
	jack_ringbuffer_free(this->audio_ring_buf);
}

void VideoFileOut::deallocate_video() {
	jack_ringbuffer_free(this->video_ring_buf);

}

int VideoFileOut::get_recording_audio() const
{
	return recording_audio;
}

void VideoFileOut::set_recording_audio(int value)
{
	recording_audio = value;
}

void VideoFileOut::wake_up_audio_write_thread()
{
	if (pthread_mutex_trylock(&this->audio_thread_info.lock) == 0) {
		pthread_cond_signal(&this->audio_thread_info.data_ready);
		pthread_mutex_unlock (&this->audio_thread_info.lock);
	}
}

void *VideoFileOut::audio_thread(void *arg) {
	int ret;
	VideoFileOut *vf = (VideoFileOut *)arg;
	int rc = pthread_setname_np(vf->get_audio_thread_info()->thread_id, "vw_audio_out");
	if (rc != 0) {
		errno = rc;
		perror("pthread_setname_np");
	}vf->allocate_audio();
	pthread_mutex_t *lock = vf->get_audio_lock();
	pthread_cond_t *data_ready;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock(lock);
	vf->set_recording_audio(1);
	while(1) {
		lock = vf->get_audio_lock();
		data_ready = vf->get_audio_data_ready();
		ret = vf->write_audio_frame();
		if (ret == 1) {
			vf->set_audio_done(1);
			break;
		}
		if (ret == 0) continue;
		if (ret == -1) pthread_cond_wait(data_ready, lock);
	}
	if (vf->get_audio_done() && vf->get_video_done() && !vf->get_trailer_written()) {
		vf->write_trailer();
	}
	pthread_mutex_unlock(lock);
	vf->deallocate_audio();
	return 0;
}

disk_thread_info_t *VideoFileOut::get_video_thread_info() {
	return &this->video_thread_info;
}

disk_thread_info_t *VideoFileOut::get_audio_thread_info() {
	return &this->audio_thread_info;
}

timespec *VideoFileOut::get_audio_first_time()
{
	return &this->audio_thread_info.stream.first_time;
}

void *VideoFileOut::video_thread(void *arg) {
	int ret;
	VideoFileOut *vf = (VideoFileOut*)arg;
	int rc = pthread_setname_np(vf->get_video_thread_info()->thread_id, "vw_video_out");
	if (rc != 0) {
		errno = rc;
		perror("pthread_setname_np");
	}
	vf->allocate_video();
	pthread_mutex_t *lock = vf->get_video_lock();
	pthread_cond_t *data_ready = vf->get_video_data_ready();
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock(lock);
	while(1) {
		ret = vf->write_video_frame();
		if (ret == 1) {
			vf->set_video_done(1);
			break;
		}
		if (ret == 0) continue;
		if (ret == -1) pthread_cond_wait(data_ready, lock);
	}
	if (vf->get_audio_done() && vf->get_video_done() && !vf->get_trailer_written()) {
		vf->write_trailer();
	}
	pthread_mutex_unlock(lock);
	vf->deallocate_video();
	return 0;
}

void VideoFileOut::start_recording(int width, int height, int bitrate) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	timespec2file_name(name, VF_STR_LEN, "Videos", "webm", &ts);
	this->width = width;
	this->height = height;
	this->bitrate = bitrate;
	this->init_output();
	this->audio_done = 0;
	this->video_done = 0;
	this->trailer_written = 0;
	this->recording_audio = 0;
	this->recording_video = 0;
	this->first_call_audio = 1;
	this->first_call_video = 1;
	recording_stopped = 0;
	recording_started = 1;
	pthread_create(&audio_thread_info.thread_id, NULL, audio_thread,
				   this);
	pthread_create(&video_thread_info.thread_id, NULL, video_thread,
				   this);

}

void VideoFileOut::write_trailer()
{
	av_write_trailer(this->video_output_context);
	this->trailer_written = 1;
	printf("TRAILER\n");
}

jack_ringbuffer_t *VideoFileOut::get_audio_ringbuffer()
{
	return this->audio_ring_buf;
}

pthread_mutex_t *VideoFileOut::get_audio_lock()
{
	return &this->audio_thread_info.lock;
}

pthread_cond_t *VideoFileOut::get_audio_data_ready()
{
	return &this->audio_thread_info.data_ready;
}

void VideoFileOut::stop_recording() {
	recording_started = 0;
	recording_stopped = 1;
}

void VideoFileOut::set_can_capture(int val)
{
	can_capture = val;
}

pthread_mutex_t *VideoFileOut::get_video_lock()
{
	return &this->video_thread_info.lock;
}

pthread_cond_t *VideoFileOut::get_video_data_ready()
{
	return &this->video_thread_info.data_ready;
}

void VideoFileOut::set_video_first_time(struct timespec val)
{
	video_first_time = val;
}

int VideoFileOut::get_video_first_call()
{
	return first_call_video;
}

void VideoFileOut::set_video_first_call(int val)
{
	first_call_video = val;
}

int VideoFileOut::get_video_done()
{
	return video_done;
}

void VideoFileOut::set_video_done(int val)
{
	video_done = val;
}

jack_ringbuffer_t *VideoFileOut::get_video_ringbuffer()
{
	return this->video_ring_buf;
}

void VideoFileOut::set_audio_samples_count(int val)
{
	this->audio_thread_info.stream.samples_count = val;
}

int VideoFileOut::get_audio_first_call()
{
	return first_call_audio;
}

void VideoFileOut::set_audio_first_call(int val)
{
	this->first_call_audio = val;
}

int VideoFileOut::get_audio_done()
{
	return audio_done;
}

void VideoFileOut::set_audio_done(int val)
{
	audio_done = val;
}

int VideoFileOut::get_trailer_written()
{
	return trailer_written;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
	AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
	char pts[AV_TS_MAX_STRING_SIZE];
	char dts[AV_TS_MAX_STRING_SIZE];
	char dur[AV_TS_MAX_STRING_SIZE];
	char pts_str[AV_TS_MAX_STRING_SIZE];
	char dts_str[AV_TS_MAX_STRING_SIZE];
	char dur_str[AV_TS_MAX_STRING_SIZE];
	av_ts_make_string(pts, pkt->pts);
	av_ts_make_time_string(pts_str, pkt->pts, time_base);
	av_ts_make_string(dts, pkt->dts);
	av_ts_make_time_string(dts_str, pkt->dts, time_base);
	av_ts_make_string(dur, pkt->duration);
	av_ts_make_time_string(dur_str, pkt->duration, time_base);
	printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s"
		   " duration_time:%s stream_index:%d\n",
		   pts, pts_str, dts, dts_str, dur, dur_str, pkt->stream_index);
}
static int write_frame(pthread_mutex_t *lock, AVFormatContext *fmt_ctx,
					   const AVRational *time_base, AVStream *st, AVPacket *pkt) {
	pthread_mutex_lock(lock);
	int ret;
	/* rescale output packet timestamp values from codec to stream timebase */
	av_packet_rescale_ts(pkt, *time_base, st->time_base);
	pkt->stream_index = st->index;
	/* Write the compressed frame to the media file. */
	ret = av_interleaved_write_frame(fmt_ctx, pkt);
	pthread_mutex_unlock(lock);
	return ret;
}

/*int VideoFileOut::write_frame(AVPacket *pkt) {
	AVFormatContext *fmt_ctx = this->video_output_context;
	const AVRational time_base = this->video_st.enc->time_base;
	AVStream *st = this->video_st.st;
	pthread_mutex_lock(&av_thread_lock);
	int ret;
	av_packet_rescale_ts(pkt, time_base, st->time_base);
	pkt->stream_index = st->index;
	if (0) log_packet(fmt_ctx, pkt);
	ret = av_interleaved_write_frame(fmt_ctx, pkt);
	pthread_mutex_unlock(&av_thread_lock);
	return ret;
}*/

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
					   AVCodec **codec, enum AVCodecID codec_id, int width,
					   int height, int video_bitrate) {
	AVCodecContext *c;
	int i;
	*codec = avcodec_find_encoder(codec_id);
	if (!(*codec)) {
		fprintf(stderr, "Could not find encoder for '%s'\n",
				avcodec_get_name(codec_id));
		exit(1);
	}
	ost->st = avformat_new_stream(oc, NULL);
	if (!ost->st) {
		fprintf(stderr, "Could not allocate stream\n");
		exit(1);
	}
	ost->st->id = oc->nb_streams-1;
	c = avcodec_alloc_context3(*codec);
	if (!c) {
		fprintf(stderr, "Could not alloc an encoding context\n");
		exit(1);
	}
	ost->enc = c;
	switch ((*codec)->type) {
		case AVMEDIA_TYPE_AUDIO:
			printf("case AUDIO\n");
			c->sample_fmt  = (*codec)->sample_fmts ?
						(*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
			c->bit_rate    = 256000;
			c->sample_rate = 48000;
			if ((*codec)->supported_samplerates) {
				c->sample_rate = (*codec)->supported_samplerates[0];
				for (i = 0; (*codec)->supported_samplerates[i]; i++) {
					if ((*codec)->supported_samplerates[i] == 48000)
						c->sample_rate = 48000;
				}
			}
			c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
			c->channel_layout = AV_CH_LAYOUT_STEREO;
			if ((*codec)->channel_layouts) {
				c->channel_layout = (*codec)->channel_layouts[0];
				for (i = 0; (*codec)->channel_layouts[i]; i++) {
					if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
						c->channel_layout = AV_CH_LAYOUT_STEREO;
				}
			}
			c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
			ost->st->time_base = (AVRational){ 1, c->sample_rate };
			break;
		case AVMEDIA_TYPE_VIDEO:
			printf("case VIDEO\n");
			c->codec_id = codec_id;
			c->bit_rate = video_bitrate;
			c->width    = width;
			c->height   = height;
			ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };
			c->time_base       = ost->st->time_base;
			c->gop_size      = 12;
			c->pix_fmt       = AV_PIX_FMT_YUV420P;
			if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
				c->max_b_frames = 2;
			}
			if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
				c->mb_decision = 2;
			}
			break;
		default:
			break;
	}
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* audio output */

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
								  uint64_t channel_layout, int sample_rate, int nb_samples) {
	AVFrame *frame = av_frame_alloc();
	int ret;
	if (!frame) {
		fprintf(stderr, "Error allocating an audio frame\n");
		exit(1);
	}
	frame->format = sample_fmt;
	frame->channel_layout = channel_layout;
	frame->sample_rate = sample_rate;
	frame->nb_samples = nb_samples;
	if (nb_samples) {
		ret = av_frame_get_buffer(frame, 0);
		if (ret < 0) {
			fprintf(stderr, "Error allocating an audio buffer\n");
			exit(1);
		}
	}
	return frame;
}

static void open_audio(AVCodec *codec, OutputStream *ost,
					   AVDictionary *opt_arg) {
	AVCodecContext *c;
	int nb_samples;
	int ret;
	char err[AV_ERROR_MAX_STRING_SIZE];
	AVDictionary *opt = NULL;
	c = ost->enc;
	av_dict_copy(&opt, opt_arg, 0);
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
		fprintf(stderr, "Could not open audio codec: %s\n", err);
		exit(1);
	}
	if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
		nb_samples = 10000;
	else
		nb_samples = c->frame_size;
	printf("number samples in frame: %d\n", nb_samples);
	ost->samples_count = 0;
	ost->frame     = alloc_audio_frame(c->sample_fmt, c->channel_layout,
									   c->sample_rate, nb_samples);
	ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_FLT, c->channel_layout,
									   c->sample_rate, nb_samples);
	ret = avcodec_parameters_from_context(ost->st->codecpar, c);
	if (ret < 0) {
		fprintf(stderr, "Could not copy the stream parameters\n");
		exit(1);
	}
	ost->swr_ctx = swr_alloc();
	if (!ost->swr_ctx) {
		fprintf(stderr, "Could not allocate resampler context\n");
		exit(1);
	}
	av_opt_set_int(ost->swr_ctx, "in_channel_count", c->channels, 0);
	av_opt_set_int(ost->swr_ctx, "in_sample_rate", c->sample_rate, 0);
	av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
	av_opt_set_int(ost->swr_ctx, "out_channel_count", c->channels, 0);
	av_opt_set_int(ost->swr_ctx, "out_sample_rate", c->sample_rate, 0);
	av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt", c->sample_fmt, 0);
	if ((ret = swr_init(ost->swr_ctx)) < 0) {
		fprintf(stderr, "Failed to initialize the resampling context\n");
		exit(1);
	}
}

int VideoFileOut::get_audio_frame(AVFrame **ret_frame) {
	OutputStream *ost = &this->audio_thread_info.stream;
	AVFrame *frame = ost->tmp_frame;
	int j, i;
	float *q = (float*)frame->data[0];
	*ret_frame = frame;
	if (this->recording_stopped) {
		printf("audio done\n");
		ret_frame = NULL;
		return 1;
	}
	uint size = sizeof(float)*frame->nb_samples*ost->enc->channels;
	if (jack_ringbuffer_read_space(this->audio_ring_buf) < size) {
		return -1;
	} else {
		frame->pts = ost->next_pts;
		ost->next_pts += frame->nb_samples;
		for (i = 0; i < frame->nb_samples; i++) {
			for (j = 0; j < ost->enc->channels; j++) {
				jack_ringbuffer_read(this->audio_ring_buf, (char *)q++, sizeof(float));
			}
		}
	}
	return 0;
}

/*
 * encode one audio frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
int VideoFileOut::write_audio_frame() {
	OutputStream *ost = &this->audio_thread_info.stream;
	AVCodecContext *c;
	AVPacket pkt;
	AVFrame *frame;
	int ret;
	int dst_nb_samples;
	char err[AV_ERROR_MAX_STRING_SIZE];
	c = ost->enc;
	ret = get_audio_frame(&frame);
	if (ret < 0) {
		return -1;
	} else if (ret == 1) {
		return 1;
	} else {
		dst_nb_samples = av_rescale_rnd(swr_get_delay(ost->swr_ctx, c->sample_rate)
										+ frame->nb_samples, c->sample_rate, c->sample_rate, AV_ROUND_UP);
		av_assert0(dst_nb_samples == frame->nb_samples);
		ret = av_frame_make_writable(ost->frame);
		if (ret < 0)
			exit(1);
		ret = swr_convert(ost->swr_ctx, ost->frame->data, dst_nb_samples,
						  (const uint8_t **)frame->data, frame->nb_samples);
		if (ret < 0) {
			fprintf(stderr, "Error while converting\n");
			exit(1);
		}
		frame = ost->frame;
		//printf("audio time: %f\n", ost->samples_count / (double) c->sample_rate);
		frame->pts = av_rescale_q(ost->samples_count,
								  (AVRational){1, c->sample_rate}, c->time_base);
		av_init_packet(&pkt);
		ret = avcodec_send_frame(c, frame);
		ost->samples_count += dst_nb_samples;
		if (ret < 0) {
			av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
			fprintf(stderr, "Error encoding audio frame: %s\n", err);
			exit(1);
		}
		while(avcodec_receive_packet(c, &pkt) >= 0) {
			ret = write_frame(&this->av_thread_lock, this->video_output_context,
							  &c->time_base, ost->st, &pkt);
			if (ret < 0) {
				av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
				fprintf(stderr, "Error while writing audio frame: %s\n", err);
				exit(1);
			}
		}
		return 0;
	}
}

int VideoFileOut::get_recording_started()
{
	return this->recording_started;
}

int VideoFileOut::get_recording_stopped()
{
	return this->recording_stopped;
}

/**************************************************************/
/* video output */

static void open_video(int width, int height, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
	int ret;
	char err[AV_ERROR_MAX_STRING_SIZE];
	AVCodecContext *c = ost->enc;
	AVDictionary *opt = NULL;

	av_dict_copy(&opt, opt_arg, 0);

	/* open the codec */
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
		fprintf(stderr, "Could not open video codec: %s\n", err);
		exit(1);
	}

	/* allocate and init a re-usable frame */
	ost->frame = av_frame_alloc();
	if (!ost->frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}
	ost->frame->format = AV_PIX_FMT_BGRA;
	ost->frame->width = width;
	ost->frame->height = height;
	ost->out_frame.size = 4 * width * height;
	ost->out_frame.data = (uint32_t *)calloc(1, ost->out_frame.size);
	av_image_fill_arrays(ost->frame->data, ost->frame->linesize,
						 (const unsigned char *)ost->out_frame.data, (AVPixelFormat)ost->frame->format,
						 ost->frame->width, ost->frame->height, 32);
	/* If the output format is not YUV420P, then a temporary YUV420P
	 * picture is needed too. It is then converted to the required
	 * output format. */
	ost->tmp_frame = NULL;
	ost->tmp_frame = av_frame_alloc();
	ost->tmp_frame->width = width;
	ost->tmp_frame->height = height;
	ost->tmp_frame->format = AV_PIX_FMT_YUV420P;
	av_image_alloc(ost->tmp_frame->data, ost->tmp_frame->linesize,
				   ost->tmp_frame->width, ost->tmp_frame->height, (AVPixelFormat)ost->tmp_frame->format, 1);
	//ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
	if (!ost->tmp_frame) {
		fprintf(stderr, "Could not allocate temporary picture\n");
		exit(1);
	}

	/* copy the stream parameters to the muxer */
	ret = avcodec_parameters_from_context(ost->st->codecpar, c);
	if (ret < 0) {
		fprintf(stderr, "Could not copy the stream parameters\n");
		exit(1);
	}
}

int VideoFileOut::get_video_frame(AVFrame **ret_frame) {
	OutputStream *ost = &this->video_thread_info.stream;
	AVCodecContext *c = ost->enc;
	*ret_frame = NULL;
	int size = ost->out_frame.size + sizeof(struct timespec);
	int space = jack_ringbuffer_read_space(video_ring_buf);
	if (space < size) {
		if (this->recording_stopped) {
			return 1;
		} else {
			return -1;
		}
	} else {
		jack_ringbuffer_read(video_ring_buf, (char *)ost->out_frame.data,
							 ost->out_frame.size);
		jack_ringbuffer_read(video_ring_buf, (char *)&ost->out_frame.ts,
							 sizeof(struct timespec));
		double diff;
		struct timespec *now;
		now = &ost->out_frame.ts;
		diff = now->tv_sec + 1e-9*now->tv_nsec - (this->video_first_time.tv_sec +
												  1e-9*this->video_first_time.tv_nsec);
		ost->next_pts = (int) c->time_base.den * diff / c->time_base.num;
		if (!ost->sws_ctx) {
			ost->sws_ctx = sws_getContext(c->width, c->height,
										  (AVPixelFormat)ost->frame->format, c->width, c->height, c->pix_fmt,
										  SCALE_FLAGS, NULL, NULL, NULL);
			if (!ost->sws_ctx) {
				fprintf(stderr,
						"Could not initialize the conversion context\n");
				exit(1);
			}
		}
		sws_scale(ost->sws_ctx, (const uint8_t * const *)ost->frame->data,
				  ost->frame->linesize, 0, c->height, ost->tmp_frame->data,
				  ost->tmp_frame->linesize);
		ost->tmp_frame->pts = ost->frame->pts = ost->next_pts;
		*ret_frame = ost->tmp_frame;
		return 0;
	}
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
int VideoFileOut::write_video_frame() {
	OutputStream *ost = &this->video_thread_info.stream;
	int ret;
	AVCodecContext *c;
	AVFrame *tframe;
	AVPacket pkt;
	char err[AV_ERROR_MAX_STRING_SIZE];
	c = ost->enc;
	ret = get_video_frame(&tframe);
	if (ret < 0) {
		if(this->recording_stopped) {
			return 1;
		}
		return -1;
	} else if (ret == 1) {
		av_init_packet(&pkt);
		ret = avcodec_send_frame(c, tframe);
		if (ret < 0) {
			av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
			fprintf(stderr, "Error encoding video frame: %s\n", err);
			exit(1);
		}
		while (avcodec_receive_packet(c, &pkt) >= 0) {
			ret = write_frame(&this->av_thread_lock, this->video_output_context,
							  &c->time_base, ost->st, &pkt);
			if (ret < 0) {
				av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
				fprintf(stderr, "Error while writing video frame: %s\n", err);
				exit(1);
			}
		}
		return 1;
	} else {
		av_init_packet(&pkt);
		/* encode the image */
		ret = avcodec_send_frame(c, tframe);
		if (ret < 0) {
			av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
			fprintf(stderr, "Error encoding video frame: %s\n", err);
			exit(1);
		}
		while (avcodec_receive_packet(c, &pkt) >= 0) {
			ret = write_frame(&this->av_thread_lock, this->video_output_context,
							  &c->time_base, ost->st, &pkt);
			if (ret < 0) {
				av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
				fprintf(stderr, "Error while writing video frame: %s\n", err);
				exit(1);
			}
		}
		return 0;
	}
}

void close_stream(OutputStream *ost)
{
	avcodec_free_context(&ost->enc);
	av_frame_free(&ost->frame);
	av_frame_free(&ost->tmp_frame);
	sws_freeContext(ost->sws_ctx);
	swr_free(&ost->swr_ctx);
}

/**************************************************************/
/* media file output */

int VideoFileOut::init_output() {
	const char *filename;
	AVOutputFormat *fmt;
	AVCodec *audio_codec, *video_codec;
	int ret;
	char err[AV_ERROR_MAX_STRING_SIZE];
	AVDictionary *opt = NULL;
	av_register_all();
	filename = this->name;
	avformat_alloc_output_context2(&this->video_output_context, NULL, NULL, filename);
	if (!this->video_output_context) {
		printf("Could not deduce output format from file extension: using MPEG.\n");
		avformat_alloc_output_context2(&this->video_output_context, NULL, "mpeg", filename);
		return 1;
	}
	fmt = this->video_output_context->oformat;
	add_stream(&this->video_thread_info.stream, this->video_output_context,
			   &video_codec, fmt->video_codec, this->width,
			   this->height, this->bitrate);
	if (fmt->audio_codec != AV_CODEC_ID_NONE) {
		add_stream(&this->audio_thread_info.stream, this->video_output_context,
				   &audio_codec, fmt->audio_codec, 0, 0, 0);
	}
	av_dict_set(&opt, "cpu-used", "-8", 0);
	av_dict_set(&opt, "deadline", "realtime", 0);
	open_video(this->width, this->height,
			   video_codec, &this->video_thread_info.stream, opt);
	open_audio(audio_codec, &this->audio_thread_info.stream, opt);
	av_dump_format(this->video_output_context, 0, filename, 1);
	if (!(fmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&this->video_output_context->pb, filename, AVIO_FLAG_WRITE);
		if (ret < 0) {
			av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
			fprintf(stderr, "Could not open '%s': %s\n", filename,
					err);
			return 1;
		}
	}

	/* Write the stream header, if any. */
	ret = avformat_write_header(this->video_output_context, &opt);
	if (ret < 0) {
		av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
		fprintf(stderr, "Error occurred when opening output file: %s\n",
				err);
		return 1;
	}

	return 0;
}
