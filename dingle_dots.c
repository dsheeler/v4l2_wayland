#include <jack/jack.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include "v4l2_wayland.h"
#include "dingle_dots.h"
#include "midi.h"

int dingle_dots_init(dingle_dots_t *dd, char *dev_name, int width, int height,
 char *video_file_name, int video_bitrate) {
	memset(dd, 0, sizeof(dingle_dots_t));
	int ret;
	dd->drawing_rect.width = width;
	dd->drawing_rect.height = height;
	dd->recording_started = 0;
  dd->recording_stopped = 0;
	dd->nports = 2;
	dd->make_new_tld = 0;
	strncpy(dd->video_file_name, video_file_name, STR_LEN);
	dd->video_bitrate = video_bitrate;
	dd->analysis_rect.width = 260;
	dd->analysis_rect.height = 148;
	dd->ascale_factor_x = dd->drawing_rect.width / (double)dd->analysis_rect.width;
  dd->ascale_factor_y = ((double)dd->drawing_rect.height) / dd->analysis_rect.height;
  dd->analysis_frame = av_frame_alloc();
  dd->analysis_frame->format = AV_PIX_FMT_ARGB;
  dd->analysis_frame->width = dd->analysis_rect.width;
  dd->analysis_frame->height = dd->analysis_rect.height;
  ret = av_image_alloc(dd->analysis_frame->data, dd->analysis_frame->linesize,
   dd->analysis_frame->width, dd->analysis_frame->height, dd->analysis_frame->format, 1);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }
	dd->analysis_resize = sws_getContext(dd->drawing_rect.width, dd->drawing_rect.height, AV_PIX_FMT_ARGB, dd->analysis_rect.width,
   dd->analysis_rect.height, AV_PIX_FMT_ARGB, SWS_BICUBIC, NULL, NULL, NULL);
	dd->doing_tld = 0;
	dd->doing_motion = 0;
	dd->motion_threshold = 0.001;
	memset(dd->sound_shapes, 0, MAX_NSOUND_SHAPES * sizeof(sound_shape));
  pthread_mutex_init(&dd->video_thread_info.lock, NULL);
  pthread_mutex_init(&dd->audio_thread_info.lock, NULL);
  pthread_mutex_init(&dd->snapshot_thread_info.lock, NULL);
  pthread_cond_init(&dd->video_thread_info.data_ready, NULL);
  pthread_cond_init(&dd->audio_thread_info.data_ready, NULL);
  pthread_cond_init(&dd->snapshot_thread_info.data_ready, NULL);
  uint32_t rb_size = 200 * 4 * 640 * 360;
	dd->snapshot_thread_info.ring_buf = jack_ringbuffer_create(rb_size);
	memset(dd->snapshot_thread_info.ring_buf->buf, 0,
	 dd->snapshot_thread_info.ring_buf->size);
	pthread_create(&dd->snapshot_thread_info.thread_id, NULL, snapshot_disk_thread,
	 dd);
	return 0;
}

int dingle_dots_deactivate_sound_shapes(dingle_dots_t *dd) {
	for (int i = 0; i < MAX_NSOUND_SHAPES; i++) {
		if (dd->sound_shapes[i].active) {
			sound_shape_deactivate(&dd->sound_shapes[i]);
		}
	}
	return 0;
}

int dingle_dots_free(dingle_dots_t *dd) {
	if (dd->csurface)
    cairo_surface_destroy(dd->csurface);
	sws_freeContext(dd->screen_resize);
  if (dd->cr) {
		cairo_destroy(dd->cr);
	}
  if (dd->analysis_resize) {
		sws_freeContext(dd->analysis_resize);
	}
  if (dd->analysis_frame) {
		av_freep(&dd->analysis_frame->data[0]);
		av_frame_free(&dd->analysis_frame);
	}
	if (dd->screen_frame) {
		av_freep(&dd->screen_frame->data[0]);
		av_frame_free(&dd->screen_frame);
	}
	return 0;
}

void dingle_dots_add_scale(dingle_dots_t *dd, midi_key_t *key, int midi_channel,
 color *c) {
	int i;
  double x_delta;
	x_delta = 1. / (key->num_steps + 1);
	for (i = 0; i < key->num_steps; i++) {
		char key_name[NCHAR];
		char base_name[NCHAR];
		char *scale;
		midi_note_to_octave_name(key->base_note, base_name);
		scale = midi_scale_id_to_text(key->scaleid);
		sprintf(key_name, "%s %s", base_name, scale);
		dingle_dots_add_note(dd, key_name, i + 1,
		 key->base_note + key->steps[i], midi_channel,
		 x_delta * (i + 1) * dd->drawing_rect.width, dd->drawing_rect.height / 2.,
		 dd->drawing_rect.width/32, c);
	}
}

int dingle_dots_add_note(dingle_dots_t *dd, char *scale_name,
 int scale_num, int midi_note, int midi_channel, double x, double y, double r, color *c) {
  char label[NCHAR], snum[NCHAR], octave_name[NCHAR];
  int i;
  double freq;
	memset(label, '\0', NCHAR * sizeof(char));
	memset(snum, '\0', NCHAR * sizeof(char));
	memset(octave_name, '\0', NCHAR * sizeof(char));
	freq = midi_to_freq(midi_note);
	if (scale_num > 0) {
		sprintf(snum, "%s", scale_name);
	}
	midi_note_to_octave_name(midi_note, octave_name);
	sprintf(label, "%.2f\n%s\n%d\n%s", freq, snum, scale_num, octave_name);
	for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
		if (dd->sound_shapes[i].active) continue;
		sound_shape_init(&dd->sound_shapes[i], label, midi_note, midi_channel,
		 x, y, r, c, dd);
		sound_shape_activate(&dd->sound_shapes[i]);
		return 0;
	}
	return -1;
}

