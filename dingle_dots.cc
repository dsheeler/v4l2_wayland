#include <jack/jack.h>

#include "dingle_dots.h"
#include "v4l2_wayland.h"
#include "v4l2.h"
#include "midi.h"


DingleDots::DingleDots() { }
int DingleDots::init(int width, int height) {
	int ret;
	this->s_pressed = 0;
	this->smdown = 0;
	this->drawing_rect.width = width;
	this->drawing_rect.height = height;

	this->animating = 0;
	this->nports = 2;
	this->make_new_tld = 0;

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
	this->selection_in_progress = 0;
	this->motion_threshold = 0.001;
	for (int i = 0; i < MAX_NUM_SOUND_SHAPES; ++i) {
		this->sound_shapes[i].clear_state();
	}
	this->setup_jack();
	int sr = jack_get_sample_rate(this->client);
	int bufsize = jack_get_buffer_size(this->client);
	float w = width / 5;
	ca_context_create(&this->event_sound_ctx);
	color c1;
	color c2;
	color_init(&c1, 0.0, 0.2, 0.4, 1.0);
	color_init(&c2, 0.0, 0.2, 0.1, 1.0);
	this->meters[0].init(this, sr, bufsize, 0.1f, 20.0f, width/2 - 2*w, height/2, w, c1);
	this->meters[1].init(this, sr, bufsize, 0.1f, 20.0f, width/2 + 2*w, height/2, w, c2);

	snapshot_shape.init("SNAPSHOT", this->drawing_rect.width / 2., this->drawing_rect.width / 16.,
						this->drawing_rect.width / 32., random_color(), this);

	pthread_mutex_init(&this->snapshot_thread_info.lock, NULL);

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

void jack_shutdown (void *) {
	printf("JACK shutdown\n");
	abort();
}

double hanning_window(int i, int N) {
	return 0.5 * (1 - cos(2 * M_PI * i / (N - 1)));
}

int process(jack_nframes_t nframes, void *arg) {
	DingleDots *dd = (DingleDots *)arg;
	if (!dd->can_process) return 0;
	midi_process_output(nframes, dd);
	for (int chn = 0; chn < dd->nports; chn++) {
		dd->in[chn] = (jack_default_audio_sample_t *)jack_port_get_buffer(dd->in_ports[chn], nframes);
		dd->out[chn] = (jack_default_audio_sample_t *)jack_port_get_buffer(dd->out_ports[chn], nframes);
	}
	if (nframes >= FFT_SIZE) {
		for (int i = 0; i < FFT_SIZE; i++) {
			dd->fftw_in[i][0] = dd->in[0][i] * hanning_window(i, FFT_SIZE);
			dd->fftw_in[i][1] = 0.0;
		}
		fftw_execute(dd->p);
	}

	for (uint i = 0; i < nframes; i++) {
		for (int chn = 0; chn < dd->nports; chn++) {
			dd->out[chn][i] = dd->in[chn][i];
		}
	}
	for (int i = 0; i < MAX_NUM_VIDEO_FILES; ++i) {
		VideoFile *vf = &dd->vf[i];
		if (vf->active && vf->audio_playing && !vf->paused) {
			jack_default_audio_sample_t sample;
			if (vf->audio_decoding_started) {
				if (vf->audio_decoding_finished &&
						jack_ringbuffer_read_space(vf->abuf) == 0) {
					vf->audio_playing = 0;
					if (pthread_mutex_trylock(&vf->video_lock) == 0) {
						pthread_cond_signal(&vf->video_data_ready);
						pthread_mutex_unlock(&vf->video_lock);
					}
				} else{
					for (uint frm = 0; frm < nframes; ++frm) {
						for (int chn = 0; chn < 2; ++chn) {
							if (jack_ringbuffer_read_space(vf->abuf) >= sizeof(sample)) {
								jack_ringbuffer_read(vf->abuf, (char *)&sample, sizeof(sample));
								dd->out[chn][frm] += sample;
							}
						}
					}
					vf->nb_frames_played += nframes;
					if (pthread_mutex_trylock(&vf->audio_lock) == 0) {
						pthread_cond_signal(&vf->audio_data_ready);
						pthread_mutex_unlock(&vf->audio_lock);
					}
				}
			}
		}
	}
	for (int chn = 0; chn < 2; chn++) {
		if (dd->meters[chn].active) dd->meters[chn].process(dd->out[chn], nframes);
	}

	uint32_t sample_size = sizeof(jack_default_audio_sample_t);
	if (dd->vfo[0].get_recording_started() && !dd->vfo[0].get_audio_done() &&
			dd->vfo[0].get_recording_audio()) {
		if (dd->vfo[0].get_audio_first_call()) {
			struct timespec *ats = dd->vfo[0].get_audio_first_time();
			clock_gettime(CLOCK_MONOTONIC, ats);
			dd->vfo[0].set_audio_samples_count(0);
			dd->vfo[0].set_audio_first_call(0);
		}
		jack_ringbuffer_t *rb = dd->vfo[0].get_audio_ringbuffer();
		for (uint i = 0; i < nframes; i++) {
			for (int chn = 0; chn < dd->nports; chn++) {
				if (jack_ringbuffer_write(rb, (const char *) (dd->out[chn]+i),
										  sample_size) < sample_size) {
					printf("jack overrun: %ld\n", ++dd->jack_overruns);
				}
			}
		}
		dd->vfo[0].wake_up_audio_write_thread();
	}
	dd->vfo[0].wake_up_audio_write_thread();
	return 0;
}

int DingleDots::setup_jack()
{
	size_t in_size;
	this->can_process = 0;
	this->jack_overruns = 0;
	if ((this->client = jack_client_open("v4l2_wayland",
									   JackNoStartServer, NULL)) == 0) {
		printf("jack server not running?\n");
		exit(1);
	}
	jack_set_process_callback(this->client, process, this);
	jack_on_shutdown(this->client, jack_shutdown, NULL);
	if (jack_activate(this->client)) {
		printf("cannot activate jack client\n");
	}
	this->nports = 2;
	this->in_ports = (jack_port_t **) malloc(sizeof(jack_port_t *) * this->nports);
	this->out_ports = (jack_port_t **) malloc(sizeof(jack_port_t *) * this->nports);
	in_size =  this->nports * sizeof (jack_default_audio_sample_t *);
	this->in = (jack_default_audio_sample_t **) malloc (in_size);
	this->out = (jack_default_audio_sample_t **) malloc (in_size);

	memset(this->in, 0, in_size);
	memset(this->out, 0, in_size);
	this->midi_ring_buf = jack_ringbuffer_create(MIDI_RB_SIZE);
	fftw_in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
	fftw_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
	p = fftw_plan_dft_1d(FFT_SIZE, fftw_in, fftw_out, FFTW_FORWARD, FFTW_ESTIMATE);
	for (int i = 0; i < this->nports; i++) {
		char name[64];
		sprintf(name, "input%d", i + 1);
		if ((this->in_ports[i] = jack_port_register (this->client, name, JACK_DEFAULT_AUDIO_TYPE,
												   JackPortIsInput, 0)) == 0) {
			printf("cannot register input port \"%s\"!\n", name);
			jack_client_close(this->client);
			exit(1);
		}
		sprintf(name, "output%d", i + 1);
		if ((this->out_ports[i] = jack_port_register (this->client, name, JACK_DEFAULT_AUDIO_TYPE,
													JackPortIsOutput, 0)) == 0) {
			printf("cannot register output port \"%s\"!\n", name);
			jack_client_close(this->client);
			exit(1);
		}
	}
	this->midi_port = jack_port_register(this->client, "output_midi",
									   JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	this->can_process = 1;
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
	for (int i = 0; i < 2; ++i) {
		Meter *m = &this->meters[i];
		if (m->active) sound_shapes.push_back(m);
	}
	//sound_shapes.push_back(&this->snapshot_shape);
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
