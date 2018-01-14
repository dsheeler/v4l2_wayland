#include <jack/jack.h>

#include "dingle_dots.h"
#include "v4l2_wayland.h"
#include "v4l2.h"
#include "midi.h"


DingleDots::DingleDots() { }
int DingleDots::init(char *dev_name, int width, int height,
					 char *video_file_name, int video_bitrate) {
	int ret;
	this->drawing_rect.width = width;
	this->drawing_rect.height = height;
	this->recording_started = 0;
	this->recording_stopped = 0;
	this->nports = 2;
	this->make_new_tld = 0;
	strncpy(this->video_file_name, video_file_name, STR_LEN);
	this->video_bitrate = video_bitrate;
	this->analysis_rect.width = 260;
	this->analysis_rect.height = 148;
	this->ascale_factor_x = this->drawing_rect.width / (double)this->analysis_rect.width;
	this->ascale_factor_y = ((double)this->drawing_rect.height) / this->analysis_rect.height;
	this->analysis_frame = av_frame_alloc();
	this->analysis_frame->format = AV_PIX_FMT_ARGB;
	this->analysis_frame->width = this->analysis_rect.width;
	this->analysis_frame->height = this->analysis_rect.height;
	ret = av_image_alloc(this->analysis_frame->data, this->analysis_frame->linesize,
						 this->analysis_frame->width, this->analysis_frame->height, (AVPixelFormat)this->analysis_frame->format, 1);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate raw picture buffer\n");
		exit(1);
	}
	this->analysis_resize = sws_getContext(this->drawing_rect.width, this->drawing_rect.height, AV_PIX_FMT_ARGB, this->analysis_rect.width,
										   this->analysis_rect.height, AV_PIX_FMT_ARGB, SWS_BICUBIC, NULL, NULL, NULL);
	this->doing_tld = 0;
	this->doing_motion = 0;
	this->motion_threshold = 0.001;
	for (int i = 0; i < MAX_NSOUND_SHAPES; ++i) {
		this->sound_shapes[i].clear_state();
	}
	string label("SNAPSHOT");
	snapshot_shape.init(label, this->drawing_rect.width / 2., this->drawing_rect.width / 16.,
						this->drawing_rect.width / 32., random_color(), this);
	pthread_mutex_init(&this->video_thread_info.lock, NULL);
	pthread_mutex_init(&this->audio_thread_info.lock, NULL);
	pthread_mutex_init(&this->snapshot_thread_info.lock, NULL);
	pthread_cond_init(&this->video_thread_info.data_ready, NULL);
	pthread_cond_init(&this->audio_thread_info.data_ready, NULL);
	pthread_cond_init(&this->snapshot_thread_info.data_ready, NULL);
	uint32_t rb_size = 200 * 4 * 640 * 360;
	this->snapshot_thread_info.ring_buf = jack_ringbuffer_create(rb_size);
	memset(this->snapshot_thread_info.ring_buf->buf, 0,
		   this->snapshot_thread_info.ring_buf->size);
	pthread_create(&this->snapshot_thread_info.thread_id, NULL, snapshot_disk_thread,
				   this);
	return 0;
}

int DingleDots::deactivate_sound_shapes() {
	for (int i = 0; i < MAX_NSOUND_SHAPES; i++) {
		if (this->sound_shapes[i].active) {
			this->sound_shapes[i].deactivate();
		}
	}
	return 0;
}

int DingleDots::free() {
	if (this->analysis_resize) {
		sws_freeContext(this->analysis_resize);
	}
	if (this->analysis_frame) {
		av_freep(&this->analysis_frame->data[0]);
		av_frame_free(&this->analysis_frame);
	}
	if (this->screen_frame) {
		av_freep(&this->screen_frame->data[0]);
		av_frame_free(&this->screen_frame);
	}
	return 0;
}

color DingleDots::random_color()
{
	struct hsva h;
	h.h = (double) rand() / RAND_MAX;
	h.v = 0.45;
	h.s = 1.0;
	h.a = 0.5;
	return hsv2rgb(&h);
}

void DingleDots::add_scale(midi_key_t *key, int midi_channel,
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
		this->add_note(key_name, i + 1,
					   key->base_note + key->steps[i], midi_channel,
					   x_delta * (i + 1) * this->drawing_rect.width, this->drawing_rect.height / 2.,
					   this->drawing_rect.width/32, c);
	}
}

int DingleDots::add_note(char *scale_name,
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
		if (this->sound_shapes[i].active) continue;
		string temp(label);
		this->sound_shapes[i].init(temp, midi_note, midi_channel,
								   x, y, r, c, this);
		this->sound_shapes[i].activate();
		return 0;
	}
	return -1;
}

