#include "dingle_dots.h"
#include "video_file_source.h"
#include <boost/bind.hpp>
#include <jack/ringbuffer.h>
#include <jack/jack.h>
#include <jack/types.h>

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
	strncpy(this->name, name, 255);
	this->rotation_radians = 0;
	this->pos.x = x;
	this->pos.y = y;
	this->z = z;
	this->have_audio = 0;
	this->nb_frames_played = 0;
	this->playing = 0;
	this->paused = 0;
	this->allocated = 0;
	this->active = 0;
	this->audio_playing = 0;
	this->video_decoding_finished = 0;
	this->video_decoding_started = 0;
	this->audio_decoding_finished = 0;
	this->audio_decoding_started = 0;
	this->easers.erase(this->easers.begin(), this->easers.end());
	pthread_create(&this->thread_id, NULL, VideoFile::thread, this);

	return 0;
}

int VideoFile::destroy() {
	av_packet_unref(&this->pkt);
	sws_freeContext(this->video_resample);
	swr_free(&this->audio_resample);
	pthread_mutex_unlock(&this->video_lock);
	jack_ringbuffer_free(this->abuf);
	jack_ringbuffer_free(this->vbuf);
	avcodec_close(this->video_dec_ctx);
	avformat_close_input(&this->fmt_ctx);
	av_freep(&this->video_dst_data[0]);
	av_freep(&this->decoded_video_frame->data[0]);
	av_frame_free(&this->decoded_video_frame);
	this->active = 0;
	this->have_audio =0;
	this->allocated = 0;
	this->video_decoding_finished = 0;
	this->video_decoding_started = 0;
	this->pos.x = this->pos.y = 0;
	this->rotation_radians = 0;
	this->playing = 0;
	pthread_cancel(this->thread_id);
	return 0;
}

int VideoFile::play() {
	this->current_playtime = 0;
	clock_gettime(CLOCK_MONOTONIC, &this->play_start_ts);
	return 0;
}

void *VideoFile::thread(void *arg) {
	int ret;
	VideoFile *vf = (VideoFile *)arg;
	int rc = pthread_setname_np(vf->thread_id, "v4l2_wayland_vf");
	if (rc != 0) {
		errno = rc;
		perror("pthread_setname_np");
	}
	av_register_all();
	vf->fmt_ctx = NULL;
	if (avformat_open_input(&vf->fmt_ctx, vf->name, NULL, NULL) < 0) {
		printf("cannot open file: %s\n", vf->name);
		return NULL;
	}
	if (avformat_find_stream_info(vf->fmt_ctx, NULL) < 0) {
		avformat_close_input(&vf->fmt_ctx);
		printf("could not find stream information\n");
		return NULL;
	}
	if (open_codec_context(&vf->audio_stream_idx, &vf->audio_dec_ctx, vf->fmt_ctx,
						   AVMEDIA_TYPE_AUDIO) >= 0) {
		vf->audio_stream = vf->fmt_ctx->streams[vf->audio_stream_idx];
		vf->audio_frame = av_frame_alloc();
		vf->have_audio = 1;
	}
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
			avcodec_close(vf->video_dec_ctx);
			avformat_close_input(&vf->fmt_ctx);
			av_frame_free(&vf->audio_frame);
			return NULL;
		}
		vf->video_dst_bufsize = ret;
	}
	vf->video_resample = sws_getContext(vf->pos.width, vf->pos.height, vf->pix_fmt,
								  vf->pos.width, vf->pos.height, AV_PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL, NULL);
	if (vf->have_audio) {
		vf->audio_resample = swr_alloc_set_opts(NULL,
												AV_CH_LAYOUT_STEREO,
												AV_SAMPLE_FMT_FLTP,
												jack_get_sample_rate(vf->dingle_dots->client),
												vf->audio_stream->codecpar->channel_layout,
												(AVSampleFormat) vf->audio_stream->codecpar->format,
												vf->audio_stream->codecpar->sample_rate, 0, NULL);
		swr_init(vf->audio_resample);
	}
	av_init_packet(&vf->pkt);
	vf->pkt.data = NULL;
	vf->pkt.size = 0;
	pthread_mutex_init(&vf->video_lock, NULL);
	pthread_cond_init(&vf->video_data_ready, NULL);
	pthread_mutex_init(&vf->audio_lock, NULL);
	pthread_cond_init(&vf->audio_data_ready, NULL);
	vf->vbuf = jack_ringbuffer_create(5*vf->video_dst_bufsize);
	memset(vf->vbuf->buf, 0, vf->vbuf->size);
	vf->abuf = jack_ringbuffer_create(
				jack_get_sample_rate(
					vf->dingle_dots->client) *
				sizeof(jack_default_audio_sample_t));
	memset(vf->abuf->buf, 0, vf->abuf->size);
	vf->decoded_video_frame = av_frame_alloc();
	vf->decoded_video_frame->format = AV_PIX_FMT_ARGB;
	vf->decoded_video_frame->width = vf->pos.width;
	vf->decoded_video_frame->height = vf->pos.height;
	av_image_alloc(vf->decoded_video_frame->data, vf->decoded_video_frame->linesize,
				   vf->decoded_video_frame->width, vf->decoded_video_frame->height,
				   (AVPixelFormat)vf->decoded_video_frame->format, 1);
	vf->allocated = 1;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock(&vf->video_lock);
	double pts;
	uint8_t **output = NULL;
	int out_samples, max_out_samples;
	if (vf->have_audio) {
		out_samples = max_out_samples = 1024;
		av_samples_alloc_array_and_samples(&output, NULL, 2, out_samples,
										   AV_SAMPLE_FMT_FLTP, 1);
	}
	while(av_seek_frame(vf->fmt_ctx, vf->video_stream_idx, 0, AVFMT_SEEK_TO_PTS) >= 0) {
		vf->play();
		vf->playing = 1;
		vf->audio_playing = 1;
		vf->video_decoding_started = 1;
		vf->audio_decoding_started = 1;
		vf->nb_frames_played = 0;
		while(av_read_frame(vf->fmt_ctx, &vf->pkt) >= 0) {
			if (vf->paused) {
				pthread_cond_wait(&vf->pause_unpuase, &vf->pause_lock);
				jack_ringbuffer_reset(vf->vbuf);
				jack_ringbuffer_reset(vf->abuf);
			}
			if (vf->have_audio && (vf->pkt.stream_index == vf->audio_stream_idx)) {
				ret = avcodec_send_packet(vf->audio_dec_ctx,&vf->pkt);
				if (ret < 0) {
					fprintf(stderr, "Error submitting the packet to the decoder\n");
				}
				while (ret >= 0) {
					ret = avcodec_receive_frame(vf->audio_dec_ctx, vf->audio_frame);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
						continue;
					else if (ret < 0) {
						fprintf(stderr, "Error during decoding\n");
						goto end;
					}
					if (av_get_channel_layout_nb_channels(
								vf->audio_frame->channel_layout) != 2) {
						fprintf(stderr, "Unsupported frame parameters\n");
						goto end;
					}
					out_samples = av_rescale_rnd(swr_get_delay(vf->audio_resample,
															   vf->audio_frame->sample_rate) +
												 vf->audio_frame->nb_samples,
												 jack_get_sample_rate(vf->dingle_dots->client),
												 vf->audio_frame->sample_rate,
												 AV_ROUND_UP);
					if (out_samples > max_out_samples) {
						av_freep(&output[0]);
						if (av_samples_alloc(output, NULL, 2, out_samples,
											 AV_SAMPLE_FMT_FLTP, 1) < 0) {
							goto end;
						}
						max_out_samples = out_samples;
					}
					out_samples = swr_convert(vf->audio_resample, output, out_samples,
											  (const uint8_t **)vf->audio_frame->extended_data,
											  vf->audio_frame->nb_samples);
					int space =	sizeof(jack_default_audio_sample_t) *
							out_samples * 2;
					int buf_space = jack_ringbuffer_write_space(vf->abuf);
					while (buf_space < space) {
						pthread_cond_wait(&vf->audio_data_ready, &vf->audio_lock);
						buf_space = jack_ringbuffer_write_space(vf->abuf);
					}
					jack_default_audio_sample_t sample;
					for (int i = 0; i < out_samples; ++i) {
						for (int j = 0; j < 2; ++j) {
							sample = ((float*)output[j])[i];
							jack_ringbuffer_write(vf->abuf, (const char *)&sample,
												  sizeof(jack_default_audio_sample_t));
						}
					}
				}
			} else if (vf->pkt.stream_index == vf->video_stream_idx) {
				ret = avcodec_send_packet(vf->video_dec_ctx,&vf->pkt);
				if (ret < 0) {
					fprintf(stderr, "Error submitting the packet to the decoder\n");
				} else {
					vf->video_frame = av_frame_alloc();
					while ((ret = avcodec_receive_frame(vf->video_dec_ctx, vf->video_frame)) >= 0) {
						if (vf->pkt.dts != AV_NOPTS_VALUE) {
							pts = av_frame_get_best_effort_timestamp(vf->video_frame);
						} else {
							pts = 0;
						}
						pts *= av_q2d(vf->video_stream->time_base);
						sws_scale(vf->video_resample, (uint8_t const * const *)vf->video_frame->data,
								  vf->video_frame->linesize, 0, vf->video_frame->height, vf->video_dst_data,
								  vf->video_dst_linesize);
						int space = vf->video_dst_bufsize + sizeof(double);
						int buf_space = jack_ringbuffer_write_space(vf->vbuf);
						while (buf_space < space) {
							gtk_widget_queue_draw(vf->dingle_dots->drawing_area);
							pthread_cond_wait(&vf->video_data_ready, &vf->video_lock);
							buf_space = jack_ringbuffer_write_space(vf->vbuf);
						}
						jack_ringbuffer_write(vf->vbuf, (const char *)&pts, sizeof(double));
						jack_ringbuffer_write(vf->vbuf, (const char *)vf->video_dst_data[0],
								vf->video_dst_bufsize);
						if (!vf->active) vf->activate();
						gtk_widget_queue_draw(vf->dingle_dots->drawing_area);
					}
					gtk_widget_queue_draw(vf->dingle_dots->drawing_area);
					av_frame_free(&vf->video_frame);
				}
			}
			gtk_widget_queue_draw(vf->dingle_dots->drawing_area);
		}
		vf->video_decoding_finished = 1;
		while (vf->playing) {
			gtk_widget_queue_draw(vf->dingle_dots->drawing_area);
			pthread_cond_wait(&vf->video_data_ready, &vf->video_lock);
		}
	}
end:
	if (vf->have_audio) {
		av_freep(&output[0]);
		av_frame_free(&vf->audio_frame);
	}
	vf->destroy();
	return 0;
}

int VideoFile::activate() {
	if (!this->active) {
		this->easers.clear();
		this->scale_to_fit(2.0);
		this->active = 1;
	}
	return 0;
}

void VideoFile::toggle_play_pause()
{
	if (!this->paused)
		this->paused = 1;
	else {
		this->paused = 0;
		if (pthread_mutex_trylock(&this->pause_lock) == 0) {
			pthread_cond_signal(&this->pause_unpuase);
			pthread_mutex_unlock(&this->pause_lock);
		}
	}
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
			diff_sec = ((double)this->nb_frames_played) / jack_get_sample_rate(this->dingle_dots->client);
			double pts;
			if (this->video_decoding_started) {
				if (!this->paused) {
					if (this->video_decoding_finished && jack_ringbuffer_read_space(this->vbuf) == 0) {
						this->playing = 0;
						if (pthread_mutex_trylock(&this->video_lock) == 0) {
							pthread_cond_signal(&this->video_data_ready);
							pthread_mutex_unlock(&this->video_lock);
						}
					} else {
						while (jack_ringbuffer_read_space(this->vbuf) >= (this->video_dst_bufsize + sizeof(double))) {
							jack_ringbuffer_peek(this->vbuf, (char *)&pts, sizeof(double));
							if (diff_sec >= pts) {
								jack_ringbuffer_read_advance(this->vbuf, sizeof(double));
								jack_ringbuffer_read(this->vbuf, (char *)this->decoded_video_frame->data[0], this->video_dst_bufsize);
								if (pthread_mutex_trylock(&this->video_lock) == 0) {
									pthread_cond_signal(&this->video_data_ready);
									pthread_mutex_unlock(&this->video_lock);
								}
							} else {
								if (pthread_mutex_trylock(&this->video_lock) == 0) {
									pthread_cond_signal(&this->video_data_ready);
									pthread_mutex_unlock(&this->video_lock);
								}
								break;
							}
						}
					}
				}
				cairo_surface_t *tsurf;
				tsurf = cairo_image_surface_create_for_data(
							(unsigned char *)this->decoded_video_frame->data[0], CAIRO_FORMAT_ARGB32,
						this->decoded_video_frame->width, this->decoded_video_frame->height, this->decoded_video_frame->linesize[0]);
				render_surface(contexts, tsurf);
				cairo_surface_destroy(tsurf);
			}
		}
	}
	return false;
}
