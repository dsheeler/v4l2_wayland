#include "dingle_dots.h"
#include "video_file_source.h"
#include <boost/bind.hpp>

VideoFile::VideoFile() { active = 0; }
int VideoFile::open_codec_context(int *stream_idx, AVCodecContext **dec_ctx,
								  AVFormatContext *fmt_ctx, enum AVMediaType type) {
	int ret, stream_index;
	AVStream *st;
	AVCodec *dec = NULL;
	AVDictionary *opts = NULL;
	ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not find %s stream\n",
				av_get_media_type_string(type));
		return ret;
	} else {
		stream_index = ret;
		st = fmt_ctx->streams[stream_index];

		/* find decoder for the stream */
		dec = avcodec_find_decoder(st->codecpar->codec_id);
		if (!dec) {
			fprintf(stderr, "Failed to find %s codec\n",
					av_get_media_type_string(type));
			return AVERROR(EINVAL);
		}

		/* Allocate a codec context for the decoder */
		*dec_ctx = avcodec_alloc_context3(dec);
		if (!*dec_ctx) {
			fprintf(stderr, "Failed to allocate the %s codec context\n",
					av_get_media_type_string(type));
			return AVERROR(ENOMEM);
		}

		/* Copy codec parameters from input stream to output codec context */
		if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
			fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
					av_get_media_type_string(type));
			return ret;
		}

		if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
			fprintf(stderr, "Failed to open %s codec\n",
					av_get_media_type_string(type));
			return ret;
		}
		*stream_idx = stream_index;
	}
	return 0;
}

int VideoFile::create(char *name, double x, double y, uint64_t z) {
	strncpy(this->name, name, 254);
	this->rotation_radians = 0;
	this->pos.x = x;
	this->pos.y = y;
	this->z = z;
	this->easers.erase(this->easers.begin(), this->easers.end());
	pthread_create(&this->thread_id, NULL, VideoFile::thread, this);
	int rc = pthread_setname_np(this->thread_id, "vw_source_video_file");
	if (rc != 0) {
		errno = rc;
		perror("pthread_setname_np");
	}
	return 0;
}

int VideoFile::destroy() {
	avcodec_close(this->video_dec_ctx);
	avformat_close_input(&this->fmt_ctx);
	av_freep(&this->video_dst_data[0]);
	av_freep(&this->decoded_frame->data[0]);
	av_frame_free(&this->decoded_frame);
	jack_ringbuffer_free(this->vbuf);
	pthread_cancel(this->thread_id);
	this->active = 0;
	this->allocated = 0;
	this->decoding_finished = 0;
	this->decoding_started = 0;
	this->pos.x = this->pos.y = 0;
	this->rotation_radians = 0;
	this->playing = 0;
	return 0;
}

int VideoFile::activate() {
	return this->activate_spin_and_scale_to_fit();
}

int VideoFile::play() {
	this->current_playtime = 0;
	clock_gettime(CLOCK_MONOTONIC, &this->play_start_ts);
	return 0;
}

void *VideoFile::thread(void *arg) {
	int ret;
	int got_frame;
	char err[AV_ERROR_MAX_STRING_SIZE];
	got_frame = 0;
	VideoFile *vf = (VideoFile *)arg;
	av_register_all();
	vf->fmt_ctx = NULL;
	if (avformat_open_input(&vf->fmt_ctx, vf->name, NULL, NULL) < 0) {
		printf("cannot open file: %s\n", vf->name);
		return NULL;
	}
	if (avformat_find_stream_info(vf->fmt_ctx, NULL) < 0) {
		printf("could not find stream information\n");
		return NULL;
	}
	av_dump_format(vf->fmt_ctx, 0, vf->name, 0);
	if (open_codec_context(&vf->video_stream_idx, &vf->video_dec_ctx, vf->fmt_ctx,
						   AVMEDIA_TYPE_VIDEO) >= 0) {
		vf->video_stream = vf->fmt_ctx->streams[vf->video_stream_idx];
		vf->pos.width = vf->video_dec_ctx->width;
		vf->pos.height = vf->video_dec_ctx->height;
		vf->pos.x = 0.5 * (vf->dingle_dots->drawing_rect.width - vf->pos.width);
		vf->pos.y = 0.5 * (vf->dingle_dots->drawing_rect.height - vf->pos.height);
		vf->pix_fmt = vf->video_dec_ctx->pix_fmt;
		ret = av_image_alloc(vf->video_dst_data, vf->video_dst_linesize,
							 vf->pos.width, vf->pos.height, AV_PIX_FMT_ARGB, 1);
		if (ret < 0) {
			printf("could not allocate raw video buffer\n");
			return NULL;
		}
		vf->video_dst_bufsize = ret;
	}
	vf->resample = sws_getContext(vf->pos.width, vf->pos.height, vf->pix_fmt,
								  vf->pos.width, vf->pos.height, AV_PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL, NULL);
	av_init_packet(&vf->pkt);
	vf->pkt.data = NULL;
	vf->pkt.size = 0;
	pthread_mutex_init(&vf->lock, NULL);
	pthread_cond_init(&vf->data_ready, NULL);
	vf->vbuf = jack_ringbuffer_create(5*vf->video_dst_bufsize);
	memset(vf->vbuf->buf, 0, vf->vbuf->size);
	vf->decoded_frame = av_frame_alloc();
	vf->decoded_frame->format = AV_PIX_FMT_ARGB;
	vf->decoded_frame->width = vf->pos.width;
	vf->decoded_frame->height = vf->pos.height;
	av_image_alloc(vf->decoded_frame->data, vf->decoded_frame->linesize,
				   vf->decoded_frame->width, vf->decoded_frame->height, (AVPixelFormat)vf->decoded_frame->format, 1);
	vf->allocated = 1;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock(&vf->lock);
	double pts;
	vf->play();
	vf->playing = 1;
	vf->fmt_ctx->iformat->flags & AVFMT_SEEK_TO_PTS;
	while(avformat_seek_file(vf->fmt_ctx, vf->video_stream_idx, 0, 0, 0, 0) >= 0) {
		vf->play();
		vf->playing = 1;

		vf->decoding_started = 1;
		while(av_read_frame(vf->fmt_ctx, &vf->pkt) >= 0) {
			if (vf->pkt.stream_index == vf->video_stream_idx) {
				vf->frame = av_frame_alloc();
				ret = avcodec_decode_video2(vf->video_dec_ctx, vf->frame,
											&got_frame, &vf->pkt);
				if (ret < 0) {
					av_make_error_string(err, AV_ERROR_MAX_STRING_SIZE, ret);
					printf("Error decoding video frame (%s)\n", err);
				}
				if (vf->pkt.dts != AV_NOPTS_VALUE) {
					pts = av_frame_get_best_effort_timestamp(vf->frame);
				} else {
					pts = 0;
				}
				pts *= av_q2d(vf->video_stream->time_base);
				if (got_frame) {
					sws_scale(vf->resample, (uint8_t const * const *)vf->frame->data,
							  vf->frame->linesize, 0, vf->frame->height, vf->video_dst_data,
							  vf->video_dst_linesize);
					int space = vf->video_dst_bufsize + sizeof(double);
					int buf_space = jack_ringbuffer_write_space(vf->vbuf);
					while (buf_space < space) {
						gtk_widget_queue_draw(vf->dingle_dots->drawing_area);
						pthread_cond_wait(&vf->data_ready, &vf->lock);
						buf_space = jack_ringbuffer_write_space(vf->vbuf);
					}
					jack_ringbuffer_write(vf->vbuf, (const char *)&pts, sizeof(double));
					jack_ringbuffer_write(vf->vbuf, (const char *)vf->video_dst_data[0],
							vf->video_dst_bufsize);
					if (!vf->active) vf->activate();
					gtk_widget_queue_draw(vf->dingle_dots->drawing_area);
				}
			}
			gtk_widget_queue_draw(vf->dingle_dots->drawing_area);
			av_frame_free(&vf->frame);
		}
		vf->decoding_finished = 1;
		while (vf->playing) {
			gtk_widget_queue_draw(vf->dingle_dots->drawing_area);
			pthread_cond_wait(&vf->data_ready, &vf->lock);
		}
	}
	av_free_packet(&vf->pkt);
	av_frame_unref(vf->frame);

	pthread_mutex_unlock(&vf->lock);
	return 0;
}

bool VideoFile::render(std::vector<cairo_t *> &contexts) {
	if (this->active) {
		if (this->playing) {
			struct timespec current_ts;
			struct timespec diff_ts;
			double diff_sec;
			clock_gettime(CLOCK_MONOTONIC, &current_ts);
			timespec_diff(&this->play_start_ts, &current_ts, &diff_ts);
			diff_sec = timespec_to_seconds(&diff_ts);
			double pts;
			if (this->decoding_started) {
				if (this->decoding_finished && jack_ringbuffer_read_space(this->vbuf) == 0) {
					this->playing = 0;
					if (pthread_mutex_trylock(&this->lock) == 0) {
						pthread_cond_signal(&this->data_ready);
						pthread_mutex_unlock(&this->lock);
					}
					//this->destroy();
					//return false;
				} else {
					while (jack_ringbuffer_read_space(this->vbuf) >= (this->video_dst_bufsize + sizeof(double))) {
						jack_ringbuffer_peek(this->vbuf, (char *)&pts, sizeof(double));
						if (diff_sec >= pts) {
							jack_ringbuffer_read_advance(this->vbuf, sizeof(double));
							jack_ringbuffer_read(this->vbuf, (char *)this->decoded_frame->data[0], this->video_dst_bufsize);
							if (pthread_mutex_trylock(&this->lock) == 0) {
								pthread_cond_signal(&this->data_ready);
								pthread_mutex_unlock(&this->lock);
							}
						} else {
							if (pthread_mutex_trylock(&this->lock) == 0) {
								pthread_cond_signal(&this->data_ready);
								pthread_mutex_unlock(&this->lock);
							}
							break;
						}
					}
				}
				cairo_surface_t *tsurf;
				tsurf = cairo_image_surface_create_for_data(
							(unsigned char *)this->decoded_frame->data[0], CAIRO_FORMAT_ARGB32,
						this->decoded_frame->width, this->decoded_frame->height, this->decoded_frame->linesize[0]);
				render_surface(contexts, tsurf);
				cairo_surface_destroy(tsurf);
			}
		}
	}
	return false;
}
