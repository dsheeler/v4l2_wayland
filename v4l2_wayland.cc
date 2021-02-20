#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include <pthread.h>
#include <getopt.h>
#include <math.h>
#include <time.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>
#include <linux/input.h>
#include <cairo/cairo.h>
#include <algorithm>
#include <vector>
#include <memory>
#include <cstring>
#include <boost/bind.hpp>
#include <gtk/gtk.h>
#include <gtkmm-3.0/gtkmm.h>

#include "dingle_dots.h"
#include "kmeter.h"
#include "sound_shape.h"
#include "midi.h"
#include "v4l2_wayland.h"
#include "v4l2.h"
#include "vwdrawable.h"
#include "video_file_source.h"
#include "video_file_out.h"

#include "easable.h"

static ccv_dense_matrix_t      *cdm = nullptr, *cdm2 = nullptr;
static ccv_tld_t               *tld = nullptr;

void errno_exit(const char *s) {
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}


int timespec2file_name(char *buf, uint len, const char *dir,
					   const char *extension,
					   struct timespec *ts) {
	uint ret;
	struct tm t;
	const char *homedir;
	if (localtime_r(&(ts->tv_sec), &t) == nullptr) {
		return 1;
	}
	if ((homedir = getenv("HOME")) == nullptr) {
		homedir = getpwuid(getuid())->pw_dir;
	}
	ret = snprintf(buf, len, "%s/%s/v4l2_wayland-", homedir, dir);
	len -= ret - 1;
	ret = strftime(&buf[strlen(buf)], len, "%Y-%m-%d-%H:%M:%S", &t);
	if (ret == 0)
		return 2;
	len -= ret - 1;
	ret = snprintf(&buf[strlen(buf)], len, ".%03d.%s",
			(int)ts->tv_nsec/1000000, extension);
	if (ret >= len)
		return 3;
	return 0;
}

void *snapshot_disk_thread (void *arg) {
	int fsize;
	int tsize;
	int space;
	cairo_surface_t *csurf;
	AVFrame *frame;
	struct timespec ts;
	char timestr[STR_LEN+1];
	tzset();
	DingleDots *dd = (DingleDots *)arg;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);
	int rc = pthread_setname_np(pthread_self(), "vw_snapshot");
	if (rc != 0) {
		errno = rc;
		perror("pthread_setname_np");
	}
	pthread_mutex_lock(&dd->snapshot_thread_info.lock);
	frame = nullptr;
	frame = av_frame_alloc();
	frame->width = dd->drawing_rect.width;
	frame->height = dd->drawing_rect.height;
	frame->format = AV_PIX_FMT_ARGB;
	av_image_alloc(frame->data, frame->linesize,
				   frame->width, frame->height, (AVPixelFormat)frame->format, 1);
	if (!frame) {
		fprintf(stderr, "Could not allocate temporary picture\n");
		exit(1);
	}

	fsize = 4 * frame->width * frame->height;
	tsize = fsize + sizeof(struct timespec);
	csurf = cairo_image_surface_create_for_data((unsigned char *)frame->data[0],
			CAIRO_FORMAT_ARGB32, frame->width, frame->height, 4*frame->width);
	while(1) {
		space = jack_ringbuffer_read_space(dd->snapshot_thread_info.ring_buf);
		while (space >= tsize) {
			jack_ringbuffer_read(dd->snapshot_thread_info.ring_buf, (char *)frame->data[0],
					fsize);
			jack_ringbuffer_read(dd->snapshot_thread_info.ring_buf, (char *)&ts,
								 sizeof(ts));
			timespec2file_name(timestr, STR_LEN, "Pictures", "png", &ts);
			cairo_surface_write_to_png(csurf, timestr);
			space = jack_ringbuffer_read_space(dd->snapshot_thread_info.ring_buf);
		}
		pthread_cond_wait(&dd->snapshot_thread_info.data_ready,
						  &dd->snapshot_thread_info.lock);
	}
	cairo_surface_destroy(csurf);
	av_freep(frame->data[0]);
	av_frame_free(&frame);
	pthread_mutex_unlock(&dd->snapshot_thread_info.lock);
	return nullptr;
}

/*ccv_tld_t *new_tld(int x, int y, int w, int h, DingleDots *dd) {
	ccv_tld_param_t p = ccv_tld_default_params;
	ccv_rect_t box = ccv_rect(x, y, w, h);
	ccv_read(dd->analysis_frame->data[0], &cdm, CCV_IO_ARGB_RAW | CCV_IO_GRAY,
			dd->analysis_rect.height, dd->analysis_rect.width, 4*dd->analysis_rect.width);
	return ccv_tld_new(cdm, box, p);
}*/

static void render_detection_box(cairo_t *cr, int initializing,
								 int x, int y, int w, int h) {
	double minimum = vw_min(w, h);
	double r = 0.05 * minimum;
	cairo_save(cr);
	//cairo_new_sub_path(cr);
	if (initializing) cairo_set_source_rgba(cr, 0.85, 0.85, 0., 0.75);
	else cairo_set_source_rgba(cr, 0.2, 0., 0.2, 0.75);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_stroke(cr);
	cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2. * M_PI);
	cairo_stroke(cr);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * M_PI);
	cairo_stroke(cr);
	cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
	cairo_stroke(cr);
	/*cairo_close_path(cr);
  if (initializing) cairo_set_source_rgba(cr, 0.85, 0.85, 0., 0.25);
  else cairo_set_source_rgba(cr, 0.2, 0., 0.2, 0.25);
  cairo_fill_preserve(cr);*/
	cairo_move_to(cr, x, 0.5 * h + y);
	cairo_line_to(cr, x + w, 0.5 * h + y);
	cairo_move_to(cr, 0.5 * w + x, y);
	cairo_line_to(cr, 0.5 * w + x, h + y);
	cairo_stroke(cr);
	cairo_restore(cr);
}



static void render_pointer(cairo_t *cr, double x, double y) {
	double l = 10.;
	cairo_save(cr);
	cairo_set_source_rgba(cr, 1, 0, 0.1, 0.75);
	cairo_translate(cr, x, y);
	cairo_rotate(cr, M_PI/4.);
	cairo_translate(cr, -x, -y);
	cairo_move_to(cr, x - l, y);
	cairo_line_to(cr, x + l, y);
	cairo_move_to(cr, x, y - l);
	cairo_line_to(cr, x, y + l);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void timespec_diff(struct timespec *start, struct timespec *stop,
				   struct timespec *result) {
	if ((stop->tv_nsec - start->tv_nsec) < 0) {
		result->tv_sec = stop->tv_sec - start->tv_sec - 1;
		result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
	} else {
		result->tv_sec = stop->tv_sec - start->tv_sec;
		result->tv_nsec = stop->tv_nsec - start->tv_nsec;
	}
	return;
}

double timespec_to_seconds(struct timespec *ts) {
	double ret;
	ret = ts->tv_sec + ts->tv_nsec / 1000000000.;
	return ret;
}

void clear(cairo_t *cr)
{
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);
}

double calculate_motion(SoundShape *ss, AVFrame *sources_frame, uint32_t *save_buf_sources,
						double width, double height)
{
	int i;
	int j;
	double fb;
	int iend;
	int jend;
	double fg;
	double fr;
	int jstart;
	int istart;
	double bw;
	double bw2;
	double diff;
	uint32_t val;
	double sum;
	uint32_t npts;

	sum = 0;
	npts = 0;
	istart = vw_min(width, vw_max(0, round(ss->pos.x -
													ss->r * ss->scale)));
	jstart = vw_min(height, vw_max(0, round(ss->pos.y -
													   ss->r * ss->scale)));
	iend = vw_max(istart, vw_min(width, round(ss->pos.x +
													ss->r * ss->scale)));
	jend = vw_max(jstart, vw_min(height, round(ss->pos.y +
													 ss->r* ss->scale)));
	for (i = istart; i < iend; i++) {
		for (j = jstart; j < jend; j++) {
			if (ss->in(i, j)) {
				val = save_buf_sources[i + j * sources_frame->width];
				fr = ((val & 0x00ff0000) >> 16) / 256.;
				fg = ((val & 0x0000ff00) >> 8) / 256.;
				fb = ((val & 0x000000ff)) / 256.;
				bw = fr * 0.3 + fg * 0.59 + fb * 0.1;
				val = ((uint32_t *)sources_frame->data[0])[i + j * sources_frame->width];
				fr = ((val & 0x00ff0000) >> 16) / 256.;
				fg = ((val & 0x0000ff00) >> 8) / 256.;
				fb = ((val & 0x000000ff)) / 256.;
				bw2 = fr * 0.3 + fg * 0.59 + fb * 0.1;
				diff = bw - bw2;
				sum += diff * diff;
				npts++;
			}
		}
	}
	return sum / npts;
}

void set_to_on_or_off(SoundShape *ss, GtkWidget *da)
{
	if (ss->double_clicked_on || ss->motion_state
			|| ss->tld_state) {
		if (!ss->on) {
			ss->set_on();
			gtk_widget_queue_draw(da);
		}
	}
	if (!ss->double_clicked_on && !ss->motion_state
			&& !ss->tld_state) {
		if (ss->on) {
			ss->set_off();
			gtk_widget_queue_draw(da);
		}
	}
}

void process_image(cairo_t *screen_cr, void *arg) {
	DingleDots *dd = (DingleDots *)arg;
	static int first_data = 1;
	static struct timespec ts, snapshot_ts;
	static ccv_comp_t newbox;
	static int made_first_tld = 0;
	std::vector<vwDrawable *> sources;
	int s, i;
	double diff;
	static uint32_t *save_buf_sources;
	int render_drawing_surf = 0;
	cairo_t *sources_cr;
	cairo_surface_t *sources_surf;
	cairo_t *drawing_cr;
	cairo_surface_t *drawing_surf;
	struct timespec start_ts, end_ts;
	clock_gettime(CLOCK_MONOTONIC, &start_ts);
	if (first_data) {
		save_buf_sources = (uint32_t *)malloc(dd->sources_frame->linesize[0] * dd->sources_frame->height);
	}
	sources_surf = cairo_image_surface_create_for_data((unsigned char *)dd->sources_frame->data[0],
			CAIRO_FORMAT_ARGB32, dd->sources_frame->width, dd->sources_frame->height,
			dd->sources_frame->linesize[0]);
	sources_cr = cairo_create(sources_surf);
	drawing_surf = cairo_image_surface_create_for_data((unsigned char *)dd->drawing_frame->data[0],
			CAIRO_FORMAT_ARGB32, dd->drawing_frame->width, dd->drawing_frame->height,
			dd->drawing_frame->linesize[0]);
	drawing_cr = cairo_create(drawing_surf);
	if (!first_data && (dd->doing_motion || dd->snapshot_shape.active)) {
		memcpy(save_buf_sources, dd->sources_frame->data[0], 4 * dd->sources_frame->width *
				dd->sources_frame->height);
	}
	clear(sources_cr);

	dd->get_sources(sources);
	std::sort(sources.begin(), sources.end(), [](vwDrawable *a, vwDrawable *b) { return a->z < b->z; } );
	std::vector<cairo_t *> contexts;
	cairo_save(screen_cr);
	cairo_scale(screen_cr, dd->scale, dd->scale);
	contexts.push_back(screen_cr);
	contexts.push_back(sources_cr);
	if (dd->background.active) {
		dd->background.update_easers();
		dd->background.render(contexts);
	}
	for (std::vector<vwDrawable *>::iterator it = sources.begin(); it != sources.end(); ++it) {
		(*it)->update_easers();
		(*it)->render(contexts);
	}
	if (dd->doing_motion) {
		std::vector<SoundShape *> sound_shapes;
		for (i = 0; i < MAX_NUM_SOUND_SHAPES; ++i) {
			SoundShape *s = &dd->sound_shapes[i];
			if (s->active) {
				sound_shapes.push_back(s);
			}
		}
		for (std::vector<SoundShape *>::iterator it = sound_shapes.begin(); it != sound_shapes.end(); ++it) {
			SoundShape *s = *it;
			diff = calculate_motion(s, dd->sources_frame, save_buf_sources,
										   dd->drawing_rect.width, dd->drawing_rect.height);
			if (diff > dd->motion_threshold) {
				s->set_motion_state(1);
			} else {
				s->set_motion_state(0);
			}
		}
	} else {
		for (s = 0; s < MAX_NUM_SOUND_SHAPES; s++) {
			dd->sound_shapes[s].set_motion_state(0);
		}
	}
	if (dd->snapshot_shape.active) {
		diff= calculate_motion(&dd->snapshot_shape, dd->sources_frame,
							   save_buf_sources, dd->drawing_rect.width,
							   dd->drawing_rect.height);
		if (diff >= dd->motion_threshold) {
			dd->snapshot_shape.set_motion_state(1);
		} else {
			dd->snapshot_shape.set_motion_state(0);
		}
		set_to_on_or_off(&dd->snapshot_shape, dd->drawing_area);
	}
	if (dd->do_snapshot || (dd->vfo[0].get_recording_started() &&
							!dd->vfo[0].get_recording_stopped())) {
		render_drawing_surf = 1;
	}
	if (render_drawing_surf) {
		cairo_save(drawing_cr);
		cairo_set_source_surface(drawing_cr, sources_surf, 0.0, 0.0);
		cairo_set_operator(drawing_cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(drawing_cr);
		cairo_restore(drawing_cr);
	}
	if (first_data) {
		first_data = 0;
	}
#if 0
	if (dd->doing_tld) {
		sws_scale(dd->analysis_resize, (uint8_t const * const *)dd->sources_frame->data,
				  dd->sources_frame->linesize, 0, dd->sources_frame->height, dd->analysis_frame->data, dd->analysis_frame->linesize);
		if (dd->make_new_tld == 1) {
			if (dd->user_tld_rect.width > 0 && dd->user_tld_rect.height > 0) {
				tld = new_tld(dd->user_tld_rect.x/dd->ascale_factor_x, dd->user_tld_rect.y/dd->ascale_factor_y,
							  dd->user_tld_rect.width/dd->ascale_factor_x, dd->user_tld_rect.height/dd->ascale_factor_y, dd);
			} else {
				tld = nullptr;
				dd->doing_tld = 0;
				made_first_tld = 0;
				newbox.rect.x = 0;
				newbox.rect.y = 0;
				newbox.rect.width = 0;
				newbox.rect.height = 0;
			}
			made_first_tld = 1;
			dd->make_new_tld = 0;
		} else {
			ccv_read(dd->analysis_frame->data[0], &cdm2, CCV_IO_ABGR_RAW |
					CCV_IO_GRAY, dd->analysis_rect.height, dd->analysis_rect.width, 4*dd->analysis_rect.width);
			ccv_tld_info_t info;
			newbox = ccv_tld_track_object(tld, cdm, cdm2, &info);
			cdm = cdm2;
			cdm2 = 0;
		}
		if (newbox.rect.width && newbox.rect.height &&
				made_first_tld && tld->found) {
			for (i = 0; i < MAX_NUM_SOUND_SHAPES; i++) {
				if (!dd->sound_shapes[i].active) continue;
				if (dd->sound_shapes[i].in(dd->ascale_factor_x*newbox.rect.x +
										   0.5*dd->ascale_factor_x*newbox.rect.width,
										   dd->ascale_factor_y*newbox.rect.y +
										   0.5*dd->ascale_factor_y*newbox.rect.height)) {
					dd->sound_shapes[i].tld_state = 1;
				} else {
					dd->sound_shapes[i].tld_state = 0;
				}
			}
		}
	} else {
		tld = nullptr;
		made_first_tld = 0;
		newbox.rect.x = 0;
		newbox.rect.y = 0;
		newbox.rect.width = 0;
		newbox.rect.height = 0;
	}
#endif
	for (int i = 0; i < MAX_NUM_SOUND_SHAPES; i++) {
		if (!dd->sound_shapes[i].active) continue;
		set_to_on_or_off(&dd->sound_shapes[i], dd->drawing_area);
	}
	std::vector<vwDrawable *> sound_shapes;
	std::vector<cairo_t *> ss_contexts;
	ss_contexts.push_back(screen_cr);
	if (dd->snapshot_shape.active) {
		dd->snapshot_shape.update_easers();
		dd->snapshot_shape.render(ss_contexts);
	}
	if (render_drawing_surf) {
		ss_contexts.push_back(drawing_cr);
	}
	for (i = 0; i < MAX_NUM_SOUND_SHAPES; ++i) {
		SoundShape *s = &dd->sound_shapes[i];
		if (s->active) {
			sound_shapes.push_back(s);
		}
	}
	for (i = 0; i < 2; i++) {
		sound_shapes.push_back(&dd->meters[i]);
	}
	std::sort(sound_shapes.begin(), sound_shapes.end(), [](vwDrawable *a, vwDrawable *b) { return a->z < b->z; } );
	for (std::vector<vwDrawable *>::iterator it = sound_shapes.begin(); it != sound_shapes.end(); ++it) {
		vwDrawable *d = *it;
		d->update_easers();
		if (d->active) d->render(ss_contexts);
	}
#define RENDER_KMETERS
#if defined(RENDER_KMETERS)

#endif
#if defined(RENDER_FFT)
	double space;
	double w;
	double h;
	double x;
	w = dd->drawing_rect.width / (FFT_SIZE + 1);
	space = w;
	cairo_save(drawing_cr);
	cairo_save(screen_cr);
	cairo_set_source_rgba(drawing_cr, 0, 0.25, 0, 0.5);
	cairo_set_source_rgba(screen_cr, 0, 0.25, 0, 0.5);
	for (i = 0; i < FFT_SIZE/2; i++) {
		h = ((20.0 * log(sqrt(fftw_out[i][0] * fftw_out[i][0] +
			  fftw_out[i][1] * fftw_out[i][1])) / M_LN10) + 50.) * (dd->drawing_rect.height / 50.);
		x = i * (w + space) + space;
		cairo_rectangle(drawing_cr, x, dd->drawing_rect.height - h, w, h);
		cairo_rectangle(screen_cr, x, dd->drawing_rect.height - h, w, h);
		cairo_fill(drawing_cr);
		cairo_fill(screen_cr);
	}
	cairo_restore(drawing_cr);
	cairo_restore(screen_cr);
#endif
	if (dd->smdown) {
		if (render_drawing_surf) {
			render_detection_box(drawing_cr, 1, dd->user_tld_rect.x, dd->user_tld_rect.y,
								 dd->user_tld_rect.width, dd->user_tld_rect.height);
		}
		render_detection_box(screen_cr, 1, dd->user_tld_rect.x, dd->user_tld_rect.y,
							 dd->user_tld_rect.width, dd->user_tld_rect.height);
	}
	if (dd->selection_in_progress) {
		dd->update_easers();
		if (render_drawing_surf) {
			dd->render_selection_box(drawing_cr);
		}
		dd->render_selection_box(screen_cr);
		for (i = 0; i < MAX_NUM_SOUND_SHAPES; i++) {
			SoundShape *ss = &dd->sound_shapes[i];
			if (!ss->active) continue;
			GdkRectangle ss_rect;
			ss_rect.x = ss->pos.x - ss->r * ss->scale;
			ss_rect.y = ss->pos.y - ss->r * ss->scale;
			ss_rect.width = 2 * ss->r * ss->scale;
			ss_rect.height = 2 * ss->r * ss->scale;
			if (gdk_rectangle_intersect(&ss_rect, &dd->selection_rect, nullptr)) {
				ss->selected = 1;
				ss->selected_pos.x = ss->pos.x;
				ss->selected_pos.y = ss->pos.y;
			} else {
				ss->selected = 0;
			}
		}
	}
	//
	cairo_restore(screen_cr);
	if (render_drawing_surf) {
		render_pointer(drawing_cr, dd->mouse_pos.x, dd->mouse_pos.y);
		//gtk_widget_draw(GTK_WIDGET(dd->ctl_window), drawing_cr);
	}
	render_pointer(screen_cr, dd->scale * dd->mouse_pos.x, dd->scale * dd->mouse_pos.y);
	if (dd->doing_tld) {
		if (render_drawing_surf) {
			render_detection_box(drawing_cr, 0, dd->ascale_factor_x*newbox.rect.x,
								 dd->ascale_factor_y*newbox.rect.y, dd->ascale_factor_x*newbox.rect.width,
								 dd->ascale_factor_y*newbox.rect.height);
		}
		render_detection_box(screen_cr, 0, dd->ascale_factor_x*newbox.rect.x,
							 dd->ascale_factor_y*newbox.rect.y, dd->ascale_factor_x*newbox.rect.width,
							 dd->ascale_factor_y*newbox.rect.height);
	}

	clock_gettime(CLOCK_REALTIME, &snapshot_ts);
	int drawing_size = 4 * dd->drawing_frame->width * dd->drawing_frame->height;
	uint tsize = drawing_size + sizeof(struct timespec);
	if (dd->do_snapshot) {
		ca_context_play (dd->event_sound_ctx, 0,
			CA_PROP_EVENT_ID, "camera-shutter",
			CA_PROP_EVENT_DESCRIPTION, "camera shutter",
			NULL);

		if (jack_ringbuffer_write_space(dd->snapshot_thread_info.ring_buf) >= (size_t)tsize) {
			jack_ringbuffer_write(dd->snapshot_thread_info.ring_buf, (const char *)dd->drawing_frame->data[0],
					drawing_size);
			jack_ringbuffer_write(dd->snapshot_thread_info.ring_buf, (const char *)&snapshot_ts,
								  sizeof(ts));
			if (pthread_mutex_trylock(&dd->snapshot_thread_info.lock) == 0) {
				pthread_cond_signal(&dd->snapshot_thread_info.data_ready);
				pthread_mutex_unlock(&dd->snapshot_thread_info.lock);
			}
		}
		dd->do_snapshot = 0;
	}
	VideoFileOut *vfo = &dd->vfo[0];
	if (vfo->get_recording_stopped()) {
		pthread_mutex_t *lock = vfo->get_video_lock();
		if (pthread_mutex_trylock(lock) == 0) {
			pthread_cond_signal(vfo->get_video_data_ready());
			pthread_mutex_unlock(lock);
		}
	}
	if (vfo->get_recording_started() && !vfo->get_recording_stopped()) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		if (vfo->get_video_first_call()) {
			vfo->set_video_first_call(0);
			vfo->set_video_first_time(ts);
			vfo->set_can_capture(1);
		}
		tsize = drawing_size + sizeof(struct timespec);
		jack_ringbuffer_t *rb = vfo->get_video_ringbuffer();
		if (jack_ringbuffer_write_space(rb) >= tsize) {
			jack_ringbuffer_write(rb, (const char *)dd->drawing_frame->data[0],
					drawing_size);
			jack_ringbuffer_write(rb, (const char *)&ts,
								  sizeof(struct timespec));
			pthread_mutex_t *lock = vfo->get_video_lock();
			if (pthread_mutex_trylock(lock) == 0) {
				pthread_cond_signal(vfo->get_video_data_ready());
				pthread_mutex_unlock(lock);
			}
		} else {
			fprintf(stderr, "no room on the fifo for another frame");
		}
	}
	cairo_destroy(sources_cr);
	cairo_destroy(drawing_cr);
	cairo_surface_destroy(sources_surf);
	cairo_surface_destroy(drawing_surf);
	clock_gettime(CLOCK_MONOTONIC, &end_ts);
	struct timespec diff_ts;
	timespec_diff(&start_ts, &end_ts, &diff_ts);
	printf("process_image time: %f\n", timespec_to_seconds(&diff_ts)*1000);
}

void teardown_jack(DingleDots *dd) {
	while (jack_ringbuffer_read_space(dd->midi_ring_buf)) {
		struct timespec pause;
		pause.tv_sec = 0;
		pause.tv_nsec = 1000;
		nanosleep(&pause, NULL);
	}
	jack_client_close(dd->client);
}

static gboolean configure_event_cb (GtkWidget *,
									GdkEventConfigure *event, gpointer data) {
	DingleDots *dd;
	dd = (DingleDots *)data;
	dd->scale = (double)(event->height) / dd->drawing_rect.height;
	return TRUE;
}

static gboolean window_state_event_cb (GtkWidget *,
									   GdkEventWindowState *event, gpointer   data) {
	DingleDots *dd;
	dd = (DingleDots *)data;
	if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) {
		dd->fullscreen = TRUE;
	} else {
		dd->fullscreen = FALSE;
	}
	return TRUE;
}

static gint queue_draw_timeout_cb(gpointer data) {
	DingleDots *dd;
	dd = (DingleDots *)data;
	bool xactive = false;
	for (int i = 0; i < MAX_NUM_X11; ++i) {
		if (dd->x11[i].active) {
			xactive = true;
			break;
		}
	}
	if (dd->meters[0].active || xactive ||
			dd->vfo[0].get_recording_started() &&
			!dd->vfo[0].get_recording_stopped()) {
		gtk_widget_queue_draw(dd->drawing_area);
	}
	return TRUE;
}

static gboolean draw_cb(GtkWidget *, cairo_t *cr, gpointer   data) {
	DingleDots *dd;
	dd = (DingleDots *)data;
	process_image(cr, dd);
	return TRUE;
}

void mark_hovered(bool use_sources, DingleDots *dd) {
	int found = 0;
	std::vector<vwDrawable *> sources;
	dd->get_sources(sources);
	std::vector<vwDrawable *> sound_shapes;
	dd->get_sound_shapes(sound_shapes);
	if (use_sources) {
		for (std::vector<vwDrawable *>::iterator it = sound_shapes.begin(); it != sound_shapes.end(); ++it) {
			if ((*it)->hovered == 1) {
				(*it)->hovered = 0;
				gtk_widget_queue_draw(dd->drawing_area);
			}
		}
		std::sort(sources.begin(), sources.end(), [](vwDrawable *a, vwDrawable *b) { return a->z > b->z; } );
		for (std::vector<vwDrawable *>::iterator it = sources.begin(); it != sources.end(); ++it) {
			if (found) {
				if ((*it)->hovered == 1) {
					(*it)->hovered = 0;
					gtk_widget_queue_draw(dd->drawing_area);
				}
			} else if ((*it)->in(dd->mouse_pos.x, dd->mouse_pos.y)) {
				found = 1;
				if ((*it)->hovered == 0) {
					(*it)->hovered = 1;
					gtk_widget_queue_draw(dd->drawing_area);
				}
			} else {
				if ((*it)->hovered == 1) {
					(*it)->hovered = 0;
					gtk_widget_queue_draw(dd->drawing_area);
				}
			}
		}
	} else {
		for (std::vector<vwDrawable *>::iterator it = sources.begin(); it != sources.end(); ++it) {
			if ((*it)->hovered == 1) {
				(*it)->hovered = 0;
				gtk_widget_queue_draw(dd->drawing_area);
			}
		}
		found = 0;
		std::sort(sound_shapes.begin(), sound_shapes.end(), [](vwDrawable *a, vwDrawable *b) { return a->z > b->z; } );
		for (std::vector<vwDrawable *>::iterator it = sound_shapes.begin(); it != sound_shapes.end(); ++it) {
			vwDrawable *s = *it;
			if (!found && s->in(dd->mouse_pos.x, dd->mouse_pos.y)) {
				s->hovered = 1;
				found = 1;
				gtk_widget_queue_draw(dd->drawing_area);
			} else if (s->hovered == 1) {
				s->hovered = 0;
				gtk_widget_queue_draw(dd->drawing_area);
			}
		}
	}
}

static gboolean motion_notify_event_cb(GtkWidget *,
									   GdkEventMotion *event, gpointer data) {
	int i;
	DingleDots *dd = (DingleDots *)data;
	dd->mouse_pos.x = event->x / dd->scale;
	dd->mouse_pos.y = event->y / dd->scale;
	if (!dd->dragging && !dd->selection_in_progress)
		mark_hovered(event->state & GDK_SHIFT_MASK, dd);
	if (dd->smdown) {
		dd->user_tld_rect.width = dd->mouse_pos.x - dd->mdown_pos.x;
		dd->user_tld_rect.height = dd->mouse_pos.y - dd->mdown_pos.y;
		if (dd->user_tld_rect.width < 0) {
			if (dd->user_tld_rect.width > - 20 * dd->ascale_factor_x) {
				dd->user_tld_rect.width = 20 * dd->ascale_factor_x;
			} else {
				dd->user_tld_rect.width = -dd->user_tld_rect.width;
			}
			dd->user_tld_rect.x = dd->mdown_pos.x - dd->user_tld_rect.width;
		} else if (dd->user_tld_rect.width >= 0) {
			if (dd->user_tld_rect.width < 20 * dd->ascale_factor_x) {
				dd->user_tld_rect.width = 20 * dd->ascale_factor_x;
			}
			dd->user_tld_rect.x = dd->mdown_pos.x;
		}
		if (dd->user_tld_rect.height < 0) {
			if (dd->user_tld_rect.height > - 20 * dd->ascale_factor_y) {
				dd->user_tld_rect.height = 20 * dd->ascale_factor_y;
			} else {
				dd->user_tld_rect.height = -dd->user_tld_rect.height;
			}
			dd->user_tld_rect.y = dd->mdown_pos.y - dd->user_tld_rect.height;
		} else if (dd->user_tld_rect.width >= 0) {
			if (dd->user_tld_rect.height < 20 * dd->ascale_factor_y) {
				dd->user_tld_rect.height = 20 * dd->ascale_factor_y;
			}
			dd->user_tld_rect.y = dd->mdown_pos.y;
		}
	}
	if (dd->selection_in_progress) {
		if (dd->mdown) {
			dd->selection_rect.width = dd->mouse_pos.x - dd->mdown_pos.x;
			dd->selection_rect.height = dd->mouse_pos.y - dd->mdown_pos.y;
		} else {
			dd->selection_rect.width = dd->mup_pos.x - dd->mdown_pos.x;
			dd->selection_rect.height = dd->mup_pos.y - dd->mdown_pos.y;
		}
		if (dd->selection_rect.width < 0) {
			dd->selection_rect.width = -dd->selection_rect.width;
			dd->selection_rect.x = dd->mdown_pos.x - dd->selection_rect.width;
		} else if (dd->selection_rect.width >= 0) {
			dd->selection_rect.x = dd->mdown_pos.x;
		}
		if (dd->selection_rect.height < 0) {
			dd->selection_rect.height = -dd->selection_rect.height;
			dd->selection_rect.y = dd->mdown_pos.y - dd->selection_rect.height;
		} else if (dd->selection_rect.height >= 0) {
			dd->selection_rect.y = dd->mdown_pos.y;
		}
	} else if (dd->mdown) {
		dd->dragging = 1;
	}
	if (!(event->state & GDK_SHIFT_MASK)) {
		std::vector<vwDrawable *> sound_shapes;
		for (i = 0; i < MAX_NUM_SOUND_SHAPES; ++i) {
			SoundShape *s = &dd->sound_shapes[i];
			if (s->active) sound_shapes.push_back(s);
		}
		for (int i = 0; i < 2; ++i) {
			Meter *m = &dd->meters[i];
			if (m->active) sound_shapes.push_back(m);
		}
		SnapshotShape *s = &dd->snapshot_shape;
		if (s->active) sound_shapes.push_back(s);
		std::sort(sound_shapes.begin(), sound_shapes.end(), [](vwDrawable *a, vwDrawable *b) { return a->z > b->z; } );
		for (std::vector<vwDrawable *>::iterator it = sound_shapes.begin(); it != sound_shapes.end(); ++it) {
			vwDrawable *s = *it;
			if (s->mdown) {
				if (s->selected) {
					for (std::vector<vwDrawable *>::iterator ij = sound_shapes.begin(); ij != sound_shapes.end(); ++ij) {
						vwDrawable *sj = *ij;
						if (sj->selected) {
							sj->pos.x = dd->mouse_pos.x -
									s->mdown_pos.x +
									sj->selected_pos.x;
							sj->pos.y = dd->mouse_pos.y -
									s->mdown_pos.y +
									sj->selected_pos.y;
						}
					};
				} else {
					s->drag(dd->mouse_pos.x, dd->mouse_pos.y);
				}
				break;
			}
		}
	} else {
		std::vector<vwDrawable *> sources;
		dd->get_sources(sources);
		std::sort(sources.begin(), sources.end(), [](vwDrawable *a, vwDrawable *b) { return a->z > b->z; } );
		for (std::vector<vwDrawable *>::iterator it = sources.begin(); it != sources.end(); ++it) {
			if ((*it)->active) {
				if ((*it)->mdown) {
					(*it)->drag(dd->mouse_pos.x, dd->mouse_pos.y);
					break;
				}
			}
		}
	}
	gtk_widget_queue_draw(dd->drawing_area);
	return TRUE;
}

static gboolean double_press_event_cb(GtkWidget *widget,
									  GdkEventButton *event, gpointer data) {
	DingleDots * dd = (DingleDots *)data;
	if (event->type == GDK_2BUTTON_PRESS &&
			event->button == GDK_BUTTON_PRIMARY &&
			!dd->delete_active) {
		uint8_t found = 0;
		double x, y;
		x = event->x / dd->scale;;
		y = event->y / dd->scale;
		if (!(event->state & GDK_SHIFT_MASK)) {
			for (int i = 0; i < MAX_NUM_SOUND_SHAPES; ++i) {
				if (!dd->sound_shapes[i].active) continue;
				if (dd->sound_shapes[i].in(x, y)) {
					found = 1;
					if (dd->sound_shapes[i].double_clicked_on) {
						dd->sound_shapes[i].double_clicked_on = 0;
					} else {
						dd->sound_shapes[i].double_clicked_on = 1;
					}
				}
			}
		} else {
			std::vector<VideoFile *> video_files;

			for (int i = 0; i < MAX_NUM_VIDEO_FILES; ++i) {
				VideoFile *vf = &dd->vf[i];
				if (vf->active) {
					video_files.push_back(vf);
				}
			}
			std::sort(video_files.begin(), video_files.end(), [](VideoFile *a, VideoFile *b) { return a->z > b->z; });
			for (std::vector<VideoFile *>::iterator it = video_files.begin();
				 it != video_files.end(); ++it) {
				VideoFile *vf = *it;
				if (vf->in(x, y)) {
					vf->toggle_play_pause();
					found = 1;
					break;
				}
			}
		}
		if (found) return TRUE;
		GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
		if (gtk_widget_is_toplevel(toplevel)) {
			if (!dd->fullscreen) gtk_window_fullscreen(GTK_WINDOW(toplevel));
			else gtk_window_unfullscreen(GTK_WINDOW(toplevel));
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean button_press_event_cb(GtkWidget *,
									  GdkEventButton *event, gpointer data) {
	int i;
	DingleDots * dd = (DingleDots *)data;
	dd->mouse_pos.x = event->x / dd->scale;
	dd->mouse_pos.y = event->y / dd->scale;
	if (event->button == GDK_BUTTON_PRIMARY) {
		dd->mdown = 1;
		dd->mdown_pos.x = dd->mouse_pos.x;
		dd->mdown_pos.y = dd->mouse_pos.y;
		if (!(event->state & GDK_SHIFT_MASK)) {
			std::vector<vwDrawable *> sound_shapes;
			dd->get_sound_shapes(sound_shapes);
			std::sort(sound_shapes.begin(), sound_shapes.end(), [](vwDrawable *a, vwDrawable *b) { return a->z > b->z; } );
			for (std::vector<vwDrawable *>::iterator it = sound_shapes.begin(); it != sound_shapes.end(); ++it) {
				vwDrawable *s = *it;
				if (s->in(dd->mouse_pos.x, dd->mouse_pos.y)) {
					if (dd->delete_active) {
						s->deactivate();
					} else {
						if (!s->selected) {
							for (std::vector<vwDrawable *>::iterator ij = sound_shapes.begin(); ij != sound_shapes.end(); ++ij) {
								vwDrawable *sj = *ij;
								if (sj->selected) {
									sj->selected = 0;
									gtk_widget_queue_draw(dd->drawing_area);
								}
							}
						} else {
							for (std::vector<vwDrawable *>::reverse_iterator ij = sound_shapes.rbegin(); ij != sound_shapes.rend(); ++ij) {
								vwDrawable *sj = *ij;
								if (sj->selected) {
									sj->z = dd->next_z++;
									sj->selected_pos.x = sj->pos.x;
									sj->selected_pos.y = sj->pos.y;
								}
							}
						}
						s->set_mdown(dd->mouse_pos.x, dd->mouse_pos.y, dd->next_z++);
						dd->queue_draw();
					}
					return FALSE;
				}
			}
			for (std::vector<vwDrawable *>::iterator ij = sound_shapes.begin(); ij != sound_shapes.end(); ++ij) {
				vwDrawable *sj = *ij;
				sj->selected = 0;
			}
			gtk_widget_queue_draw(dd->drawing_area);
		} else {
			std::vector<vwDrawable *> sources;
			dd->get_sources(sources);
			std::sort(sources.begin(), sources.end(), [](vwDrawable *a, vwDrawable *b) { return a->z > b->z; } );
			for (std::vector<vwDrawable *>::iterator it = sources.begin(); it != sources.end(); ++it) {
				if ((*it)->in(dd->mouse_pos.x, dd->mouse_pos.y)) {
					if (dd->delete_active) {
						(*it)->deactivate();
					} else {
						(*it)->set_mdown(dd->mouse_pos.x, dd->mouse_pos.y,
										 event->state & GDK_CONTROL_MASK ?
											 (*it)->z : dd->next_z++);
					}
					break;
				}
			}
			return false;
		}
		dd->set_selecting_on();
		dd->selection_rect.x = dd->mouse_pos.x;
		dd->selection_rect.y = dd->mouse_pos.y;
		dd->selection_rect.width = 0;
		dd->selection_rect.height = 0;
		return FALSE;
	}

//	if (!dd->shift_pressed && event->button == GDK_BUTTON_SECONDARY) {
//		dd->smdown = 1;
//		dd->mdown_pos.x = dd->mouse_pos.x;
//		dd->mdown_pos.y = dd->mouse_pos.y;
//		dd->user_tld_rect.x = dd->mdown_pos.x;
//		dd->user_tld_rect.y = dd->mdown_pos.y;
//		dd->user_tld_rect.width = 20 * dd->ascale_factor_x;
//		dd->user_tld_rect.height = 20 * dd->ascale_factor_y;
//		return TRUE;
//	}
//	if (dd->doing_tld && dd->shift_pressed) {
//		if (event->button == GDK_BUTTON_SECONDARY) {
//			dd->doing_tld = 0;
//			return TRUE;
//		}
//	}
	return FALSE;
}

static gboolean button_release_event_cb(GtkWidget *,
										GdkEventButton *event, gpointer data) {
	DingleDots * dd;
	int i;
	dd = (DingleDots *)data;
	dd->mouse_pos.x = event->x / dd->scale;
	dd->mouse_pos.y = event->y / dd->scale;
	dd->mup_pos.x = dd->mouse_pos.x;
	dd->mup_pos.y = dd->mouse_pos.y;
	mark_hovered(event->state & GDK_SHIFT_MASK, dd);
	if (event->button == GDK_BUTTON_PRIMARY) {
		if (!dd->dragging && !dd->selection_in_progress) {

			for (i = 0; i < MAX_NUM_SOUND_SHAPES; i++) {
				if (!dd->sound_shapes[i].active) continue;
				if (!dd->sound_shapes[i].mdown) {
					dd->sound_shapes[i].selected = 0;
				}
			}
		}
		for (i = 0; i < MAX_NUM_SOUND_SHAPES; i++) {
			if (!dd->sound_shapes[i].active) continue;
			dd->sound_shapes[i].mdown = 0;
		}
		for (int i = 0; i < 2; ++i) {
			dd->meters[i].mdown = 0;
		}
		dd->snapshot_shape.mdown = 0;
		std::vector<vwDrawable *> sources;
		dd->get_sources(sources);
		for (std::vector<vwDrawable *>::iterator it = sources.begin(); it != sources.end(); ++it) {
			if ((*it)->active) {
				(*it)->mdown = 0;
			}
		}
		dd->mdown = 0;
		dd->dragging = 0;
		if (dd->selection_in_progress) {
			Easer *e = new Easer();
			e->initialize(dd, dd, EASER_LINEAR, boost::bind(&DingleDots::set_selection_box_alpha, dd, _1), 1.0, 0.0, 0.2);
			e->add_finish_action(boost::bind(&DingleDots::set_selecting_off, dd));
			dd->add_easer(e);
			e->start();
		}
		gtk_widget_queue_draw(dd->drawing_area);
		return TRUE;
	} /*else if (!(event->state & GDK_SHIFT_MASK) && event->button == GDK_BUTTON_SECONDARY) {
		dd->smdown = 0;
		dd->make_new_tld = 1;
		dd->doing_tld = 1;
		return TRUE;
	}*/
	return FALSE;
}

void apply_scrolling_operations_to_list(GdkEventScroll *event, DingleDots *dd,
										std::vector<vwDrawable *> drawables)
{
	gboolean up = FALSE;
	if (event->delta_y == -1.0) {
		up = TRUE;
	}
	for (std::vector<vwDrawable *>::iterator it = drawables.begin(); it != drawables.end(); ++it) {
		if ((*it)->in(dd->mouse_pos.x, dd->mouse_pos.y)) {
			if (event->state & GDK_MOD1_MASK) {
				double inc = 0.025;
				double o = (*it)->get_opacity();
				(*it)->set_opacity(o + (up ? inc: -inc));
			} else if (dd->s_pressed){
				double inc = 0.05;
				(*it)->set_scale((*it)->get_scale() + (up ? inc: -inc));
			} else if (event->state & GDK_CONTROL_MASK) {
				double inc = 2 * M_PI / 180;
				(*it)->rotate(up ? inc : -inc);
			}
			gtk_widget_queue_draw(dd->drawing_area);
			break;
		}
	}
}

static gboolean scroll_cb(GtkWidget *, GdkEventScroll *event,
						  gpointer data) {
	DingleDots *dd = (DingleDots *)data;

	if (!(event->state & GDK_SHIFT_MASK)) {
		std::vector<vwDrawable *> sound_shapes;
		dd->get_sound_shapes(sound_shapes);
		std::sort(sound_shapes.begin(), sound_shapes.end(), [](vwDrawable *a, vwDrawable *b) { return a->z > b->z; } );
		apply_scrolling_operations_to_list(event, dd, sound_shapes);
		return TRUE;
	} else {
		std::vector<vwDrawable *> sources;
		dd->get_sources(sources);
		std::sort(sources.begin(), sources.end(), [](vwDrawable *a, vwDrawable *b) { return a->z > b->z; } );
		apply_scrolling_operations_to_list(event, dd, sources);
		return TRUE;
	}

	return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
							 gpointer data) {
	DingleDots *dd = (DingleDots *)data;
	if (event->keyval == GDK_KEY_q && (!dd->vfo[0].get_recording_started() ||
									   dd->vfo[0].get_recording_stopped())) {
		g_application_quit(dd->app);
	} else if (event->keyval == GDK_KEY_r && (!dd->vfo[0].get_recording_started())) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->record_button), 1);
		return TRUE;
	} else if (event->keyval == GDK_KEY_r && (dd->vfo[0].get_recording_started())) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->record_button), 0);
		return TRUE;
	} else if (event->keyval == GDK_KEY_d) {
		if (!dd->delete_active) {
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->delete_button), 1);
		} else {
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->delete_button), 0);
		}
	} else if (event->keyval == GDK_KEY_s ||
			   event->keyval == GDK_KEY_S) {
		dd->s_pressed = 1;
		return TRUE;
	} else if (event->keyval == GDK_KEY_Shift_L ||
			   event->keyval == GDK_KEY_Shift_R) {
		if (!dd->selection_in_progress)
			mark_hovered(1, dd);
		return TRUE;
	} else if (event->keyval == GDK_KEY_Escape && dd->fullscreen) {
		GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
		if (gtk_widget_is_toplevel(toplevel)) {
			gtk_window_unfullscreen(GTK_WINDOW(toplevel));
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean on_key_release(GtkWidget *, GdkEventKey *event,
							   gpointer data) {
	DingleDots *dd = (DingleDots *)data;
	if (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S) {
		dd->s_pressed = 0;
	}
	if (event->keyval == GDK_KEY_Shift_L ||
			event->keyval == GDK_KEY_Shift_R) {
		if (!dd->dragging && !dd->selection_in_progress) mark_hovered(0, dd);
		return TRUE;
	}
	return FALSE;
}

static gboolean delete_cb(GtkWidget *, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	if (!dd->delete_active) {
		dd->delete_active = 1;
	} else {
		dd->delete_active = 0;
	}
	return TRUE;
}

static gboolean record_cb(GtkWidget *, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	if (!dd->vfo[0].get_recording_started()) {
		int bitrate;
		bitrate = atoi(gtk_entry_get_text(GTK_ENTRY(dd->bitrate_entry)));
		dd->vfo[0].start_recording(dd->drawing_rect.width,
								   dd->drawing_rect.height,
								   bitrate);
		gtk_widget_queue_draw(dd->drawing_area);
	} else if (!dd->vfo[0].get_recording_stopped()) {
		dd->vfo[0].stop_recording();
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->record_button), 0);
		//gtk_widget_set_sensitive(dd->record_button, 0);
	}
	return TRUE;
}

static gboolean quit_cb(GtkWidget *, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	g_application_quit(dd->app);
	return TRUE;
}

static gboolean background_cb(GtkWidget *, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	GtkWidget *dialog;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	gint res;
	dialog = gtk_file_chooser_dialog_new ("Open File",
										  GTK_WINDOW(dd->ctl_window),
										  action,
										  "Cancel",
										  GTK_RESPONSE_CANCEL,
										  "Open",
										  GTK_RESPONSE_ACCEPT,
										  NULL);
	res = gtk_dialog_run (GTK_DIALOG (dialog));
	if (res == GTK_RESPONSE_ACCEPT) {
		gchar *fname;
		GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
		fname = gtk_file_chooser_get_filename(chooser);
		std::string filename(fname);
		Sprite *s = &dd->background;
		if (s->active) {
			s->free();
		}
		s->create(&filename, dd->next_z++, dd);

		gtk_widget_queue_draw(dd->drawing_area);
		g_free (fname);
	}
	gtk_widget_destroy (dialog);
	return TRUE;
}

static gboolean show_sprite_cb(GtkWidget *, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	GtkWidget *dialog;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	gint res;
	dialog = gtk_file_chooser_dialog_new ("Open File",
										  GTK_WINDOW(dd->ctl_window),
										  action,
										  "Cancel",
										  GTK_RESPONSE_CANCEL,
										  "Open",
										  GTK_RESPONSE_ACCEPT,
										  NULL);
	res = gtk_dialog_run (GTK_DIALOG (dialog));
	if (res == GTK_RESPONSE_ACCEPT) {
		gchar *fname;
		GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
		fname = gtk_file_chooser_get_filename(chooser);
		std::string filename(fname);
		for (int index = 0; index < MAX_NUM_SPRITES; ++index) {
			Sprite *s = &dd->sprites[index];
			if (!s->active) {
				s->create(&filename, dd->next_z++, dd);
				gtk_widget_queue_draw(dd->drawing_area);
				break;
			}
		}
		g_free (fname);
	}
	gtk_widget_destroy (dialog);
	return TRUE;
}

static gboolean play_file_cb(GtkWidget *, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	GtkWidget *dialog;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	gint res;
	dialog = gtk_file_chooser_dialog_new ("Open File",
										  GTK_WINDOW(dd->ctl_window),
										  action,
										  "Cancel",
										  GTK_RESPONSE_CANCEL,
										  "Open",
										  GTK_RESPONSE_ACCEPT,
										  NULL);
	res = gtk_dialog_run (GTK_DIALOG (dialog));
	if (res == GTK_RESPONSE_ACCEPT) {
		char *filename;
		GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
		filename = gtk_file_chooser_get_filename (chooser);
		for (int i = 0; i < MAX_NUM_VIDEO_FILES; ++i) {
			if(!dd->vf[i].allocated) {
				dd->vf[i].dingle_dots = dd;
				dd->vf[i].create(filename, 0.0, 0.0, dd->next_z++);
				break;
			}
		}
		g_free (filename);
	}
	gtk_widget_destroy (dialog);
	return TRUE;
}

static gboolean motion_cb(GtkWidget *widget, gpointer data) {
	DingleDots *dd = (DingleDots*) data;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		dd->doing_motion = 1;
	} else {
		dd->doing_motion = 0;
	}
	return TRUE;
}

static gboolean meter_cb(GtkWidget *widget, gpointer data) {
	DingleDots *dd = (DingleDots*) data;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		for (int i = 0; i < 2; ++i) {
			dd->meters[i].activate();
		}
	} else {
		for (int i = 0; i < 2; ++i) {
			dd->meters[i].deactivate();
		}
	}
	return TRUE;
}

static gboolean snapshot_shape_cb(GtkWidget *widget, gpointer data) {
	DingleDots *dd = (DingleDots*) data;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		dd->snapshot_shape.activate();
		//dd->show_shapshot_shape = 1;
	} else {
		dd->snapshot_shape.deactivate();
		//dd->show_shapshot_shape = 0;
	}
	gtk_widget_queue_draw(dd->drawing_area);
	return TRUE;
}

static gboolean rand_color_cb(GtkWidget *widget, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		dd->use_rand_color_for_scale = 1;
		gtk_widget_set_sensitive(dd->scale_color_button, 0);
	} else {
		dd->use_rand_color_for_scale = 0;
		gtk_widget_set_sensitive(dd->scale_color_button, 1);
	}
	return TRUE;
}

static gboolean set_modes_cb(GtkWidget *widget, gpointer data) {
	GtkComboBoxText *resolution_combo = (GtkComboBoxText *) data;
	gtk_combo_box_text_remove_all(resolution_combo);
	std::vector<std::pair<int, int>> width_height;
	const gchar *name = gtk_combo_box_get_active_id(GTK_COMBO_BOX(widget));
	V4l2::get_dimensions(name, width_height);
	int index = 0;

	for (std::vector<std::pair<int,int>>::iterator it = width_height.begin();
		 it != width_height.end(); ++it) {
		char index_str[64];
		char mode_str[64];
		memset(index_str, '\0', sizeof(index_str));
		memset(mode_str, '\0', sizeof(index_str));
		snprintf(index_str, 63, "%d", index);
		snprintf(mode_str, 63, "%dx%d", width_height.at(index).first,
				 width_height.at(index).second);
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(resolution_combo), index_str, mode_str);
		++index;
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(resolution_combo), 0);
	return TRUE;
}

static gboolean x11_cb(GtkWidget *widget, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;

	if (dd->use_window_x11) {
		GtkWidget *dialog;
		GtkWidget *dialog_content;
		GtkWidget *combo;
		int res;
		GtkDialogFlags flags = (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT);
		dialog = gtk_dialog_new_with_buttons("Select Window", GTK_WINDOW(dd->ctl_window),
											 flags, "Open", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_REJECT, NULL);
		dialog_content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
		combo = gtk_combo_box_text_new();
		for (Window win : X11::get_top_level_windows()) {
			gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo),
									  X11::get_window_name(win).c_str(),
									  X11::get_window_name(win).c_str());
		}
		gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

		gtk_container_add(GTK_CONTAINER(dialog_content), combo);
		gtk_widget_show_all(dialog);
		res = gtk_dialog_run(GTK_DIALOG(dialog));
		if (res == GTK_RESPONSE_ACCEPT) {
			const gchar *name;
			name = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
			for (int i = 0; i < MAX_NUM_X11; ++i) {
				if (!dd->x11[i].allocated) {
					dd->x11[i].init_window(dd, X11::get_window_from_string(string(name)));
					dd->x11[i].activate();
					break;
				}
			}
		}
		gtk_widget_destroy (dialog);
	} else {
		int x, y, w, h;
		x = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dd->x11_x_input));
		y = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dd->x11_y_input));
		w = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dd->x11_w_input));
		h = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dd->x11_h_input));
		for (int i = 0; i < MAX_NUM_X11; ++i) {
			if (!dd->x11[i].allocated) {
				dd->x11[i].init(dd, x, y, w, h);
				dd->x11[i].activate();
			}
		}
	}
	return TRUE;
}

static gboolean x11_win_cb(GtkWidget *widget, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		dd->use_window_x11 = 1;
		gtk_widget_set_sensitive(dd->x11_x_input, 0);
		gtk_widget_set_sensitive(dd->x11_y_input, 0);
		gtk_widget_set_sensitive(dd->x11_h_input, 0);
		gtk_widget_set_sensitive(dd->x11_w_input, 0);

	} else {
		dd->use_window_x11 = 0;
		gtk_widget_set_sensitive(dd->x11_x_input, 1);
		gtk_widget_set_sensitive(dd->x11_y_input, 1);
		gtk_widget_set_sensitive(dd->x11_h_input, 1);
		gtk_widget_set_sensitive(dd->x11_w_input, 1);
	}
	return TRUE;
}

static gboolean text_cb(GtkWidget *, gpointer data) {
	DingleDots *dd;
	dd = (DingleDots *)data;
	char *text = (char *)gtk_entry_get_text(GTK_ENTRY(dd->text_entry));
	for(int i = 0; i < MAX_NUM_TEXTS; i++) {
		Text *t = &dd->text[i];
		if (!t->allocated) {
			t->create(text, (char*)dd->text_font_entry->get_text().c_str(), dd);
			t->active = true;
			vwColor vwc;
			if (dd->use_rand_color_for_text) {
				vwc = dd->random_vw_color();
			} else {
				Gdk::RGBA *c = &dd->text_color;
				vwc.set_rgba(c->get_red(), c->get_green(), c->get_blue(), c->get_alpha());
			}
			t->set_color_rgba(1.0, 1.0, 1.0, 0.0);
			double duration = 2.0;
			Easer *es = new Easer();
			double target_s = vwc.get(S);
			es->initialize(dd, t, EASER_LINEAR,
						   std::bind(&Text::set_color_saturation, t, std::placeholders::_1),
						   0, target_s, duration);
			Easer *eh = new Easer();
			double target_hue = vwc.get(H);
			eh->initialize(dd, &dd->text[i], EASER_LINEAR,
						 std::bind(&Text::set_color_hue, &dd->text[i], std::placeholders::_1),
						 0., target_hue, duration);
			Easer *ev = new Easer();
			double target_v = vwc.get(V);
			ev->initialize(dd, &dd->text[i], EASER_LINEAR,
						 std::bind(&Text::set_color_value, &dd->text[i], std::placeholders::_1),
						 1., target_v, duration);
			Easer *ea = new Easer();
			double target_a = vwc.get(A);
			ea->initialize(dd, &dd->text[i], EASER_LINEAR,
						 std::bind(&Text::set_color_alpha, &dd->text[i], std::placeholders::_1),
						 1., target_a, duration);
			Easer *eo = new Easer();
			eo->initialize(dd, &dd->text[i], EASER_LINEAR,
						 std::bind(&Text::set_color_alpha, &dd->text[i], std::placeholders::_1),
						 0., 1.0, duration);

			eo->add_finish_easer(eh);
			eo->add_finish_easer(ev);
			eo->add_finish_easer(es);
			eo->add_finish_easer(ea);
			eo->start();
			break;
		}
	}
}

static gboolean text_rand_color_cb(GtkWidget *widget, gpointer data) {
	DingleDots *dd = (DingleDots *)data;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		dd->use_rand_color_for_text = 1;
		gtk_widget_set_sensitive(GTK_WIDGET(dd->text_color_button->gobj()), 0);
	} else {
		dd->use_rand_color_for_text = 0;
		gtk_widget_set_sensitive(GTK_WIDGET(dd->text_color_button->gobj()), 1);
	}
	return TRUE;
}

static gboolean text_color_cb(GtkWidget *widget, gpointer data) {
	DingleDots *dd;
	dd = (DingleDots *) data;
	gtk_color_button_get_rgba(GTK_COLOR_BUTTON(widget), dd->text_color.gobj());
}

static gboolean camera_cb(GtkWidget *, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	GtkWidget *dialog;
	GtkWidget *dialog_content;
	GtkWidget *combo;
	GtkWidget *resolution_combo;
	int res;
	GtkDialogFlags flags = (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT);
	dialog = gtk_dialog_new_with_buttons("Open Camera", GTK_WINDOW(dd->ctl_window),
										 flags, "Open", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_REJECT, NULL);
	dialog_content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	combo = gtk_combo_box_text_new();

	std::map<std::string, std::string> files;
	V4l2::list_devices(files);
	for (std::map<std::string, std::string>::iterator it = files.begin();
		 it != files.end(); ++it) {
		char index_str[64];
		memset(index_str, '\0', sizeof(index_str));
		snprintf(index_str, 63,"%s", it->first.c_str());
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), index_str, it->second.c_str());
	}
	resolution_combo = gtk_combo_box_text_new();
	gtk_container_add(GTK_CONTAINER(dialog_content), combo);

	g_signal_connect(combo, "changed", G_CALLBACK(set_modes_cb), resolution_combo);

	Gtk::CheckButton *mirrored_button = new Gtk::CheckButton("MIRRORED");
	gtk_container_add(GTK_CONTAINER(dialog_content), GTK_WIDGET(mirrored_button->gobj()));

	//gtk_combo_box_set_active(GTK_COMBO_BOX(resolution_combo), 1);
	gtk_container_add(GTK_CONTAINER(dialog_content), resolution_combo);
	gtk_widget_show_all(dialog);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
	res = gtk_dialog_run(GTK_DIALOG(dialog));
	if (res == GTK_RESPONSE_ACCEPT) {
		const gchar *name = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
		gchar *res_str = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(resolution_combo));
		bool mirrored = mirrored_button->get_active();
		gchar *w, *h;
		w = strsep(&res_str, "x");
		h = strsep(&res_str, "x");
		for(int i = 0; i < MAX_NUM_V4L2; i++) {
			if (!dd->v4l2[i].allocated) {
				dd->v4l2[i].init(dd, (char *)name, atof(w), atof(h), mirrored, dd->next_z++);
				break;
			}
		}
	}
	gtk_widget_destroy (dialog);
	return TRUE;
}

static gboolean snapshot_cb(GtkWidget *, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	dd->do_snapshot = 1;
	return TRUE;
}

static gboolean make_scale_cb(GtkWidget *, gpointer data) {
	DingleDots * dd;
	GdkRGBA gc;
	color c;
	dd = (DingleDots *)data;
	char *text_scale;
	char text_note[4];
	int channel;
	text_scale =
			gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dd->scale_combo));
	sprintf(text_note, "%s",
			gtk_combo_box_get_active_id(GTK_COMBO_BOX(dd->note_combo)));
	midi_key_t key;
	midi_key_init_by_scale_id(&key, atoi(text_note),
							  midi_scale_text_to_id(text_scale));
	if (dd->use_rand_color_for_scale) {
		c = dd->random_color();
	} else {
		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dd->scale_color_button), &gc);
		color_init(&c, gc.red, gc.green, gc.blue, gc.alpha);
	}
	channel = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dd->channel_combo)));
	dd->add_scale(&key, channel, &c);
	return TRUE;
}

static void bitrate_combo_change_cb(GtkComboBox *combo, gpointer user_data) {
	DingleDots *dd = (DingleDots *)user_data;
	int bitrate = atoi(gtk_combo_box_get_active_id(combo));
	gtk_entry_set_text(GTK_ENTRY(dd->bitrate_entry), g_strdup_printf("%i", bitrate));
}

static void activate(GtkApplication *app, gpointer user_data) {
	GtkWidget *window;
	GtkWidget *drawing_area;
	GtkWidget *note_hbox;
	GtkWidget *toggle_hbox;
	GtkWidget *record_hbox;
	GtkWidget *bitrate_label;
	GtkWidget *vbox;
	GtkWidget *qbutton;
	GtkWidget *mbutton;
	GtkWidget *meter_button;
	GtkWidget *play_file_button;
	GtkWidget *show_sprite_button;
	GtkWidget *snapshot_button;
	GtkWidget *snapshot_shape_button;
	GtkWidget *camera_button;
	GtkWidget *background_button;
	GtkWidget *x11_hbox;
	GtkWidget *x11_x_label;
	GtkWidget *x11_y_label;
	GtkWidget *x11_w_label;
	GtkWidget *x11_h_label;
	GtkWidget *x11_button;
	GtkWidget *make_scale_button;
	GtkWidget *aspect;
	GtkWidget *channel_hbox;
	GtkWidget *channel_label;
	GtkWidget *text_button;
	GtkWidget *text_hbox;
	Gtk::CheckButton *text_rand_color;
	DingleDots *dd;
	int ret;

	dd = (DingleDots *)user_data;
	//ccv_enable_default_cache();
	dd->user_tld_rect.width = dd->drawing_rect.width/5.;
	dd->user_tld_rect.height = dd->drawing_rect.height/5.;
	dd->user_tld_rect.x = dd->drawing_rect.width/2.0;
	dd->user_tld_rect.y = dd->drawing_rect.height/2.0 - 0.5 * dd->user_tld_rect.height;
	dd->drawing_frame = av_frame_alloc();
	dd->drawing_frame->format = AV_PIX_FMT_ARGB;
	dd->drawing_frame->width = dd->drawing_rect.width;
	dd->drawing_frame->height = dd->drawing_rect.height;
	ret = av_image_alloc(dd->drawing_frame->data, dd->drawing_frame->linesize,
						 dd->drawing_frame->width, dd->drawing_frame->height, (AVPixelFormat)dd->drawing_frame->format, 1);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate raw picture buffer\n");
		exit(1);
	}
	dd->sources_frame = av_frame_alloc();
	dd->sources_frame->format = AV_PIX_FMT_ARGB;
	dd->sources_frame->width = dd->drawing_rect.width;
	dd->sources_frame->height = dd->drawing_rect.height;
	ret = av_image_alloc(dd->sources_frame->data, dd->sources_frame->linesize,
						 dd->sources_frame->width, dd->sources_frame->height, (AVPixelFormat)dd->sources_frame->format, 1);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate sources buffer\n");
		exit(1);
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
	gtk_window_set_deletable(GTK_WINDOW (window), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(window), dd->drawing_rect.width, dd->drawing_rect.height);
	dd->ctl_window = gtk_application_window_new(app);
	gtk_window_set_keep_above(GTK_WINDOW(dd->ctl_window), TRUE);
	gtk_window_set_title(GTK_WINDOW (dd->ctl_window), "Controls");
	g_signal_connect (window, "destroy", G_CALLBACK (quit_cb), dd);
	g_signal_connect (dd->ctl_window, "destroy", G_CALLBACK (quit_cb), dd);
	aspect = gtk_aspect_frame_new(NULL, 0.5, 0.5, ((float)dd->drawing_rect.width)/dd->drawing_rect.height, FALSE);
	gtk_frame_set_shadow_type(GTK_FRAME(aspect), GTK_SHADOW_NONE);
	drawing_area = gtk_drawing_area_new();
	dd->drawing_area = drawing_area;
	GdkGeometry size_hints;
	size_hints.min_aspect = ((double)dd->drawing_rect.width)/dd->drawing_rect.height;
	size_hints.max_aspect = ((double)dd->drawing_rect.width)/dd->drawing_rect.height;
	gtk_window_set_geometry_hints(GTK_WINDOW(window), NULL, &size_hints,
								  GDK_HINT_ASPECT);
	gtk_container_add(GTK_CONTAINER(aspect), drawing_area);
	note_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	toggle_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	dd->record_button = gtk_toggle_button_new_with_label("RECORD");
	record_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	GtkWidget *bitrate_combo = gtk_combo_box_text_new();
	g_signal_connect(bitrate_combo, "changed", G_CALLBACK(bitrate_combo_change_cb), dd);

	dd->bitrate_entry = gtk_entry_new();
	gtk_entry_set_alignment(GTK_ENTRY(dd->bitrate_entry), 1.0);
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(bitrate_combo), "53000000", "2160p");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(bitrate_combo), "24000000", "1440p");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(bitrate_combo), "12000000", "1080p");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(bitrate_combo), "7500000", "720p");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(bitrate_combo), "4000000", "480p");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(bitrate_combo), "1500000", "360p");
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(bitrate_combo), "7500000");

	dd->delete_button = gtk_toggle_button_new_with_label("DELETE");
	qbutton = gtk_button_new_with_label("QUIT");
	mbutton = gtk_check_button_new_with_label("MOTION DETECTION");
	meter_button = gtk_check_button_new_with_label("SOUND METERS");
	snapshot_shape_button = gtk_check_button_new_with_label("MOTION SNAPSHOT SHAPE");
	play_file_button = gtk_button_new_with_label("PLAY VIDEO FILE");
	background_button = gtk_button_new_with_label("BACKGROUND IMAGE");
	show_sprite_button = gtk_button_new_with_label("SHOW IMAGE");
	dd->x11_win_button = gtk_check_button_new_with_label("PICK WINDOW");
	snapshot_button = gtk_button_new_with_label("TAKE SNAPSHOT");
	camera_button = gtk_button_new_with_label("OPEN CAMERA");
	Gtk::RadioButton *x11_use_window_button = new Gtk::RadioButton("PICK WINDOW");
	x11_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	x11_x_label = gtk_label_new("X:");
	x11_y_label = gtk_label_new("Y:");
	x11_w_label = gtk_label_new("WIDTH:");
	x11_h_label = gtk_label_new("HEIGHT:");
	int w, h;
	X11::get_display_dimensions(&w, &h);
	dd->x11_x_input = gtk_spin_button_new_with_range(0, w-1, 10);
	dd->x11_y_input = gtk_spin_button_new_with_range(0, h-1, 10);
	dd->x11_w_input = gtk_spin_button_new_with_range(0, w, 10);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(dd->x11_w_input), w);
	dd->x11_h_input = gtk_spin_button_new_with_range(0, h, 10);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(dd->x11_h_input), h);
	x11_button = gtk_button_new_with_label("CAPTURE X11");
	gtk_box_pack_start(GTK_BOX(x11_hbox), dd->x11_win_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(x11_hbox), x11_x_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(x11_hbox), dd->x11_x_input, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(x11_hbox), x11_y_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(x11_hbox), dd->x11_y_input, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(x11_hbox), x11_w_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(x11_hbox), dd->x11_w_input, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(x11_hbox), x11_h_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(x11_hbox), dd->x11_h_input, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(x11_hbox), x11_button, TRUE, TRUE, 0);
	dd->text_entry = gtk_entry_new();
	dd->text_font_entry = new Gtk::Entry();
	text_button = gtk_button_new_with_label("CREATE TEXT");
	Gtk::Label *text_font_label = new Gtk::Label("FONT: ");
	Gtk::Label *text_label = new Gtk::Label("TEXT: ");
	dd->text_color.set_red(1.0);
	dd->text_color.set_green(1.0);
	dd->text_color.set_blue(1.0);
	dd->text_color.set_alpha(1.0);
	text_rand_color = new Gtk::CheckButton("RANDOM COLOR");
	dd->text_color_button = new Gtk::ColorButton(dd->text_color);
	dd->text_color_button->set_use_alpha(TRUE);
	text_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_box_pack_start(GTK_BOX(text_hbox), GTK_WIDGET(text_label->gobj()), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(text_hbox), dd->text_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(text_hbox), GTK_WIDGET(text_font_label->gobj()), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(text_hbox), GTK_WIDGET(dd->text_font_entry->gobj()), FALSE,FALSE, 0);
	gtk_box_pack_start(GTK_BOX(text_hbox), GTK_WIDGET(dd->text_color_button->gobj()), FALSE,FALSE, 0);
	gtk_box_pack_start(GTK_BOX(text_hbox), GTK_WIDGET(text_rand_color->gobj()), FALSE,FALSE, 0);

	gtk_box_pack_start(GTK_BOX(text_hbox), text_button, FALSE, FALSE, 0);
	bitrate_label = gtk_label_new("VIDEO BITRATE:");
	GtkWidget *bitrate_suffix = gtk_label_new("(bps)");
	gtk_box_pack_start(GTK_BOX(toggle_hbox), mbutton, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(toggle_hbox), snapshot_shape_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(toggle_hbox), meter_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(record_hbox), bitrate_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(record_hbox), bitrate_combo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(record_hbox), dd->bitrate_entry, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(record_hbox), bitrate_suffix, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(record_hbox), dd->record_button, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), record_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), snapshot_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), toggle_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), play_file_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), background_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), show_sprite_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), camera_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), x11_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), text_hbox, FALSE, FALSE, 0);
	dd->scale_combo = gtk_combo_box_text_new();
	int i = 0;
	const char *name = midi_scale_id_to_text(i);
	while (strcmp("None", name) != 0) {
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(dd->scale_combo), NULL, name);
		name = midi_scale_id_to_text(++i);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(dd->scale_combo), 0);
	dd->note_combo = gtk_combo_box_text_new();
	for (int i = 0; i < 128; i++) {
		char id[4], text[NCHAR];
		sprintf(id, "%d", i);
		midi_note_to_octave_name(i, text);
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(dd->note_combo), id, text);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(dd->note_combo), 60);
	dd->channel_combo = gtk_combo_box_text_new();
	for (int i = 0; i < 16; ++i) {
		char id[3];
		char text[NCHAR];
		snprintf(text, 3, "%d", i);
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(dd->channel_combo), id, text);

	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(dd->channel_combo), 0);
	channel_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	channel_label = gtk_label_new("CHANNEL");
	gtk_box_pack_start(GTK_BOX(channel_hbox), dd->channel_combo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(channel_hbox), channel_label, FALSE, FALSE, 0);
	make_scale_button = gtk_button_new_with_label("MAKE SCALE");
	dd->rand_color_button = gtk_check_button_new_with_label("RANDOM COLOR");
	GdkRGBA gc;
	gc.red = 0;
	gc.green = 0.2;
	gc.blue = 0.3;
	gc.alpha = 0.5;
	dd->scale_color_button = gtk_color_button_new_with_rgba(&gc);
	gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dd->scale_color_button),
									TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), note_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note_hbox), dd->scale_combo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note_hbox), dd->note_combo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note_hbox), dd->scale_color_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note_hbox), dd->rand_color_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note_hbox), channel_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note_hbox), dd->channel_combo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note_hbox), make_scale_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), dd->delete_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), qbutton, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER(window), aspect);
	gtk_container_add (GTK_CONTAINER (dd->ctl_window), vbox);
	g_signal_connect(dd->record_button, "clicked", G_CALLBACK(record_cb), dd);
	g_signal_connect(dd->delete_button, "clicked", G_CALLBACK(delete_cb), dd);
	g_signal_connect(qbutton, "clicked", G_CALLBACK(quit_cb), dd);
	g_signal_connect(snapshot_button, "clicked", G_CALLBACK(snapshot_cb), dd);
	g_signal_connect(snapshot_shape_button, "clicked", G_CALLBACK(snapshot_shape_cb), dd);
	g_signal_connect(camera_button, "clicked", G_CALLBACK(camera_cb), dd);
	g_signal_connect(x11_button, "clicked", G_CALLBACK(x11_cb), dd);
	g_signal_connect(dd->x11_win_button, "clicked", G_CALLBACK(x11_win_cb), dd);
	g_signal_connect(make_scale_button, "clicked", G_CALLBACK(make_scale_cb), dd);
	g_signal_connect(text_button, "clicked", G_CALLBACK(text_cb), dd);
	g_signal_connect(dd->text_color_button->gobj(), "color_set", G_CALLBACK(text_color_cb), dd);
	g_signal_connect(text_rand_color->gobj(), "toggled", G_CALLBACK(text_rand_color_cb), dd);

	g_signal_connect(mbutton, "toggled", G_CALLBACK(motion_cb), dd);
	g_signal_connect(meter_button, "toggled", G_CALLBACK(meter_cb), dd);
	g_signal_connect(play_file_button, "clicked", G_CALLBACK(play_file_cb), dd);
	g_signal_connect(background_button, "clicked", G_CALLBACK(background_cb), dd);
	g_signal_connect(show_sprite_button, "clicked", G_CALLBACK(show_sprite_cb), dd);
	g_signal_connect(dd->rand_color_button, "toggled", G_CALLBACK(rand_color_cb), dd);
	g_signal_connect (drawing_area, "draw",
					  G_CALLBACK (draw_cb), dd);
	g_signal_connect (drawing_area,"configure-event",
					  G_CALLBACK (configure_event_cb), dd);
	g_signal_connect (window,"window-state-event",
					  G_CALLBACK (window_state_event_cb), dd);
	g_signal_connect (drawing_area, "motion-notify-event",
					  G_CALLBACK (motion_notify_event_cb), dd);
	g_signal_connect (drawing_area, "scroll-event",
					  G_CALLBACK (scroll_cb), dd);
	g_signal_connect (drawing_area, "button-press-event",
					  G_CALLBACK (button_press_event_cb), dd);
	g_signal_connect (drawing_area, "button-press-event",
					  G_CALLBACK (double_press_event_cb), dd);
	g_signal_connect (drawing_area, "button-release-event",
					  G_CALLBACK (button_release_event_cb), dd);
	g_signal_connect (window, "key-press-event",
					  G_CALLBACK (on_key_press), dd);
	g_signal_connect (window, "key-release-event",
					  G_CALLBACK (on_key_release), dd);
	gtk_widget_set_events(window, gtk_widget_get_events(window)
						  | GDK_WINDOW_STATE);
	gtk_widget_set_events (drawing_area, gtk_widget_get_events (drawing_area)
						   | GDK_BUTTON_PRESS_MASK | GDK_2BUTTON_PRESS
						   | GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK
						   | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_SMOOTH_SCROLL_MASK);
	gtk_widget_show_all (window);
	gtk_widget_show_all (dd->ctl_window);
}

static void mainloop(DingleDots *dd) {
	dd->app = G_APPLICATION(gtk_application_new("org.dsheeler.v4l2_wayland",
												G_APPLICATION_NON_UNIQUE));
	g_signal_connect(dd->app, "activate", G_CALLBACK (activate), dd);
	g_application_run(G_APPLICATION(dd->app), 0, NULL);
}

static void signal_handler(int) {
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

void setup_signal_handler() {
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
}

static void usage(DingleDots *, FILE *fp, int, char **argv)
{
	fprintf(fp,
			"Usage: %s [options]\n\n"
			"Options:\n"
			"-h | --help          Print this message\n"
			"-w	| --width         display width in pixels"
			"-g | --height        display height in pixels"
			"",
			argv[0]);
}

static const char short_options[] = "d:ho:b:w:g:x:y:";

static const struct option
		long_options[] = {
{ "help",   no_argument,       NULL, 'h' },
{ "width", required_argument, NULL, 'w' },
{ "height", required_argument, NULL, 'g' },
{ 0, 0, 0, 0 }
};

/*int main(int argc, char **argv) {
	DingleDots dingle_dots;
	int width = 1280;
	int height = 720;
	srand(time(NULL));
	for (;;) {
		int idx;
		int c;
		c = getopt_long(argc, argv,
						short_options, long_options, &idx);
		if (-1 == c)
			break;
		switch (c) {
			case 0: // getopt_long() flag
				break;

			case 'w':
				width = atoi(optarg);
				break;
			case 'g':
				height = atoi(optarg);
				break;
			case 'h':
				usage(&dingle_dots, stdout, argc, argv);
				exit(EXIT_SUCCESS);
			default:
				usage(&dingle_dots, stderr, argc, argv);
				exit(EXIT_FAILURE);
		}
	}
	dingle_dots.init(width, height);
	setup_jack(&dingle_dots);
	setup_signal_handler();
	g_timeout_add(40, queue_draw_timeout_cb, &dingle_dots);
	mainloop(&dingle_dots);
	dingle_dots.deactivate_sound_shapes();
	dingle_dots.free();
	teardown_jack(&dingle_dots);
	fprintf(stderr, "\n");
	return 0;
}*/

	int main(int argc, char **argv) {
		DingleDots dingle_dots;
		int width = 1280;
		int height = 720;
		srand(time(NULL));
		int rc = pthread_setname_np(pthread_self(), "vw_gui");
		if (rc != 0) {
			errno = rc;
			perror("pthread_setname_np");
		}

		for (;;) {
			int idx;
			int c;
			c = getopt_long(argc, argv,
							short_options, long_options, &idx);
			if (-1 == c)
				break;
			switch (c) {
				case 0: /* getopt_long() flag */
					break;
				case 'w':
					width = atoi(optarg);
					break;
				case 'g':
					height = atoi(optarg);
					break;
				case 'h':
					usage(&dingle_dots, stdout, argc, argv);
					exit(EXIT_SUCCESS);
				default:
					usage(&dingle_dots, stderr, argc, argv);
					exit(EXIT_FAILURE);
			}
		}
		dingle_dots.init(width, height);
		setup_signal_handler();
		g_timeout_add(40, queue_draw_timeout_cb, &dingle_dots);
		mainloop(&dingle_dots);
		dingle_dots.deactivate_sound_shapes();
		dingle_dots.free();
		teardown_jack(&dingle_dots);
		fprintf(stderr, "\n");
		return 0;
	}
