#include <jack/jack.h>

#include "dingle_dots.h"
#include "v4l2_wayland.h"
#include "v4l2.h"
#include "midi.h"


DingleDots::DingleDots() { }
int DingleDots::init(int width, int height,
					 int video_bitrate) {
	int ret;
	this->s_pressed = 0;
	this->smdown = 0;
	this->drawing_rect.width = width;
	this->drawing_rect.height = height;
	this->recording_started = 0;
	this->recording_stopped = 0;
	this->animating = 0;
	this->nports = 2;
	this->make_new_tld = 0;
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
	this->show_shapshot_shape = 0;
	this->mdown = 0;
	this->dragging = 0;
	this->set_selecting_off();
	this->motion_threshold = 0.001;
	for (int i = 0; i < MAX_NUM_SOUND_SHAPES; ++i) {
		this->sound_shapes[i].clear_state();
	}
	snapshot_shape.init("SNAPSHOT", this->drawing_rect.width / 2., this->drawing_rect.width / 16.,
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
	for (int i = 0; i < MAX_NUM_SOUND_SHAPES; i++) {
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
	double r =this->drawing_rect.width/32.;
	x_delta = 1. / (key->num_steps + 1);
	double y = r + (1.0 * rand()) / RAND_MAX * (this->drawing_rect.height - 2*r);
	for (i = 0; i < key->num_steps; i++) {
		char key_name[NCHAR];
		char base_name[NCHAR];
		const char *scale;
		midi_note_to_octave_name(key->base_note, base_name);
		scale = midi_scale_id_to_text(key->scaleid);
		sprintf(key_name, "%s %s", base_name, scale);
		this->add_note(key_name, i + 1,
					   key->base_note + key->steps[i], midi_channel,
					   x_delta * (i + 1) * this->drawing_rect.width, y,
					   this->drawing_rect.width/32, c);
	}
}

void DingleDots::render_selection_box(cairo_t *cr) {
	cairo_save(cr);
	cairo_rectangle(cr, floor(this->selection_rect.x)+0.5, floor(this->selection_rect.y)+0.5,
					this->selection_rect.width, this->selection_rect.height);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.2 * this->selection_box_alpha);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1 * this->selection_box_alpha);
	cairo_set_line_width(cr, 0.5);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void DingleDots::set_selecting_on() {
	this->selection_in_progress = 1;
	this->selection_box_alpha = 1.0;
	this->easers.clear();
	gtk_widget_queue_draw(this->drawing_area);
}

void DingleDots::set_selecting_off() {
	this->selection_in_progress = 0;
	gtk_widget_queue_draw(this->drawing_area);
}

double DingleDots::get_selection_box_alpha() const
{
	return selection_box_alpha;
}

void DingleDots::set_selection_box_alpha(double value)
{
	selection_box_alpha = value;
	gtk_widget_queue_draw(this->drawing_area);

}

uint8_t DingleDots::get_animating() const {
	return animating;
}

void DingleDots::set_animating(const uint8_t &value) {
	animating = value;
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
	for (i = 0; i < MAX_NUM_SOUND_SHAPES; i++) {
		SoundShape *s = &this->sound_shapes[i];
		if (s->active) continue;

		s->init(label, midi_note, midi_channel,
			   x, y, r, c, this);
		s->activate();

		return 0;
	}
	return -1;
}

void DingleDots::get_sound_shapes(std::vector<Drawable *> &sound_shapes)
{
	for (int i = 0; i < MAX_NUM_SOUND_SHAPES; ++i) {
		SoundShape *ss = &this->sound_shapes[i];
		if (ss->active) sound_shapes.push_back(ss);
	}
	sound_shapes.push_back(&this->snapshot_shape);
}

void DingleDots::get_sources(std::vector<Drawable *> &list)
{
	for (int i = 0; i < MAX_NUM_V4L2; i++) {
		if (this->v4l2[i].active) {
			list.push_back(&this->v4l2[i]);
		}
	}
	for (int j = 0; j < MAX_NUM_VIDEO_FILES; j++) {
		if (this->vf[j].active) {
			list.push_back(&this->vf[j]);
		}
	}
	for (int i = 0; i < MAX_NUM_SPRITES; i++) {
		if (this->sprites[i].active) {
			list.push_back(&this->sprites[i]);
		}
	}
}
