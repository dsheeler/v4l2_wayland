#include "muxing.h"
#include "v4l2_wayland.h"

extern OutputStream video_st;
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
  AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
  printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s"
   " duration_time:%s stream_index:%d\n",
   av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
   av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
   av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
   pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx,
 const AVRational *time_base, AVStream *st, AVPacket *pkt) {
  pthread_mutex_lock(&av_thread_lock);
  int ret;
  /* rescale output packet timestamp values from codec to stream timebase */
  av_packet_rescale_ts(pkt, *time_base, st->time_base);
  pkt->stream_index = st->index;
  /* Write the compressed frame to the media file. */
  /*log_packet(fmt_ctx, pkt);*/
  ret = av_interleaved_write_frame(fmt_ctx, pkt);
  pthread_mutex_unlock(&av_thread_lock);
  return ret;
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec, enum AVCodecID codec_id) {
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
      c->bit_rate = stream_bitrate;
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

static void open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost,
 AVDictionary *opt_arg) {
  AVCodecContext *c;
  int nb_samples;
  int ret;
  AVDictionary *opt = NULL;
  c = ost->enc;
  av_dict_copy(&opt, opt_arg, 0);
  ret = avcodec_open2(c, codec, &opt);
  av_dict_free(&opt);
  if (ret < 0) {
    fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
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

int get_audio_frame(OutputStream *ost, AVFrame **ret_frame) {
  AVFrame *frame = ost->tmp_frame;
  int j, i, v;
  float *q = (float*)frame->data[0];
  *ret_frame = frame;
  if (recording_stopped) {
    printf("audio done\n");
    ret_frame = NULL;
    return 1;
  }
  int size = sizeof(float)*frame->nb_samples*ost->enc->channels;
  if (jack_ringbuffer_read_space(audio_ring_buf) < size) {
    return -1;
  } else {
    frame->pts = ost->next_pts;
    ost->next_pts += frame->nb_samples;
    for (i = 0; i < frame->nb_samples; i++) {
      for (j = 0; j < ost->enc->channels; j++) {
        jack_ringbuffer_read(audio_ring_buf, (char *)q++, sizeof(float));
      }
    }
  }
  return 0;
}

/*
 * encode one audio frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
int write_audio_frame(dingle_dots_t *dd, AVFormatContext *oc,
 OutputStream *ost) {
  AVCodecContext *c;
  AVPacket pkt;
  AVFrame *frame;
  int ret;
  int got_packet;
  int dst_nb_samples;
  c = ost->enc;
  ret = get_audio_frame(ost, &frame);
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
    printf("audio time: %f\n", ost->samples_count / (double) c->sample_rate);
    frame->pts = av_rescale_q(ost->samples_count,
     (AVRational){1, c->sample_rate}, c->time_base);
    av_init_packet(&pkt);
    ret = avcodec_send_frame(c, frame);
    ost->samples_count += dst_nb_samples;
    if (ret < 0) {
      fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
      exit(1);
    }
    while(avcodec_receive_packet(c, &pkt) >= 0) {
      ret = write_frame(oc, &c->time_base, ost->st, &pkt);
      if (ret < 0) {
        fprintf(stderr, "Error while writing audio frame: %s\n",
        av_err2str(ret));
        exit(1);
      }
    }
    return 0;
  }
}

/**************************************************************/
/* video output */

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
  AVFrame *picture;
  int ret;
  picture = av_frame_alloc();
  if (!picture)
    return NULL;
  picture->format = pix_fmt;
  picture->width  = width;
  picture->height = height;
  /* allocate the buffers for the frame data */
  ret = av_frame_get_buffer(picture, 32);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate frame data.\n");
    exit(1);
  }
  return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = ost->enc;
    AVDictionary *opt = NULL;

    av_dict_copy(&opt, opt_arg, 0);

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* allocate and init a re-usable frame */
    ost->frame = av_frame_alloc();
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    ost->frame->format = AV_PIX_FMT_RGB32;
    ost->frame->width = width;
    ost->frame->height = height;
    ost->out_frame.size = 4 * width * height;
    ost->out_frame.data = calloc(1, ost->out_frame.size);
    av_image_fill_arrays(ost->frame->data, ost->frame->linesize,
     (const unsigned char *)ost->out_frame.data, ost->frame->format,
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
     ost->tmp_frame->width, ost->tmp_frame->height, ost->tmp_frame->format, 1);
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
    clock_gettime(CLOCK_MONOTONIC, &ost->last_time);
}

int get_video_frame(OutputStream *ost, AVFrame **ret_frame) {
  AVCodecContext *c = ost->enc;
  *ret_frame = NULL;
  int size = ost->out_frame.size + sizeof(struct timespec);
  int space = jack_ringbuffer_read_space(video_ring_buf);
  if (space < size) {
    if (recording_stopped) {
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
    diff = now->tv_sec + 1e-9*now->tv_nsec - (ost->first_time.tv_sec +
     1e-9*ost->first_time.tv_nsec);
    printf("video time : %f\n", diff);
    ost->next_pts = (int) c->time_base.den * diff / c->time_base.num;
    if (!ost->sws_ctx) {
      ost->sws_ctx = sws_getContext(c->width, c->height,
       AV_PIX_FMT_RGB32, c->width, c->height, c->pix_fmt,
       SCALE_FLAGS, NULL, NULL, NULL);
      if (!ost->sws_ctx) {
        fprintf(stderr,
         "Could not initialize the conversion context\n");
        exit(1);
      }
    }
    printf("before sws_scale get_video_frame \n");
    printf("ost->frame->data: %d, ost->tmp_frame->data: %d\n", ost->frame->data,
        ost->tmp_frame->data);
    sws_scale(ost->sws_ctx, (const uint8_t * const *)ost->frame->data,
     ost->frame->linesize, 0, c->height, ost->tmp_frame->data,
     ost->tmp_frame->linesize);
    printf("after sws_scale get_video_frame\n");
    ost->tmp_frame->pts = ost->frame->pts = ost->next_pts;
    ost->last_time = *now;
    *ret_frame = ost->tmp_frame;
    return 0;
  }
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
int write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
  int ret;
  AVCodecContext *c;
  AVFrame *tframe;
  int got_packet = 0;
  AVPacket pkt;
  c = ost->enc;
  ret = get_video_frame(ost, &tframe);
  if (ret < 0) {
    if(recording_stopped) {
      return 1;
    }
    return -1;
  } else if (ret == 1) {
    av_init_packet(&pkt);
    ret = avcodec_send_frame(c, tframe);
    if (ret < 0) {
      fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
      exit(1);
    }
    while (avcodec_receive_packet(c, &pkt) >= 0) {
      ret = write_frame(oc, &c->time_base, ost->st, &pkt);
      if (ret < 0) {
        fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
        exit(1);
      }
    }
    return 1;
  } else {
    av_init_packet(&pkt);
    /* encode the image */
    ret = avcodec_send_frame(c, tframe);
    if (ret < 0) {
      fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
      exit(1);
    }
    while (avcodec_receive_packet(c, &pkt) >= 0) {
      ret = write_frame(oc, &c->time_base, ost->st, &pkt);
      if (ret < 0) {
        fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
        exit(1);
      }
    }
    return 0;
  }
}

void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

/**************************************************************/
/* media file output */

int init_output(dingle_dots_t *dd) {
  const char *filename;
  AVOutputFormat *fmt;
  AVCodec *audio_codec, *video_codec;
  int ret;
  int have_video = 0, have_audio = 0;
  int encode_video = 0, encode_audio = 0;
  AVDictionary *opt = NULL;
  int i;
  av_register_all();
  filename = out_file_name;
  avformat_alloc_output_context2(&oc, NULL, NULL, filename);
  if (!oc) {
    printf("Could not deduce output format from file extension: using MPEG.\n");
    avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
  }
  if (!oc)
    return 1;
  fmt = oc->oformat;
  add_stream(dd->video_thread_info->stream, oc,
   &video_codec, fmt->video_codec);
  have_video = 1;
  encode_video = 1;
  if (fmt->audio_codec != AV_CODEC_ID_NONE) {
    add_stream(dd->audio_thread_info->stream, oc,
     &audio_codec, fmt->audio_codec);
    have_audio = 1;
    encode_audio = 1;
  }
  open_video(oc, video_codec, dd->video_thread_info->stream, opt);
  //open_video(oc, video_codec, &video_st, opt);
  open_audio(oc, audio_codec, dd->audio_thread_info->stream, opt);
  av_dump_format(oc, 0, filename, 1);
  if (!(fmt->flags & AVFMT_NOFILE)) {
    ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
      fprintf(stderr, "Could not open '%s': %s\n", filename,
          av_err2str(ret));
      return 1;
    }
  }

  /* Write the stream header, if any. */
  ret = avformat_write_header(oc, &opt);
  if (ret < 0) {
    fprintf(stderr, "Error occurred when opening output file: %s\n",
     av_err2str(ret));
    return 1;
  }

  return 0;
}
