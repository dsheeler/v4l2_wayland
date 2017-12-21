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
#include <fftw3.h>
#include <algorithm>
#include <vector>
#include <memory>

#include "dingle_dots.h"
#include "kmeter.h"
#include "muxing.h"
#include "sound_shape.h"
#include "midi.h"
#include "v4l2_wayland.h"
#include "v4l2.h"
#include "draggable.h"
#include "video_file_source.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define FFT_SIZE 256
#define MIDI_RB_SIZE 1024 * sizeof(struct midi_message)


static void isort(void *base, size_t nmemb, size_t size,
				  int (*compar)(const void *, const void *));

static void visort(void *base, size_t nmemb, size_t size,
				   int (*compar)(const void *, const void *));

fftw_complex                   *fftw_in, *fftw_out;
fftw_plan                      p;
static ccv_dense_matrix_t      *cdm = 0, *cdm2 = 0;
static ccv_tld_t               *tld = 0;

jack_ringbuffer_t        *video_ring_buf, *audio_ring_buf;
const size_t              sample_size = sizeof(jack_default_audio_sample_t);
pthread_mutex_t           av_thread_lock = PTHREAD_MUTEX_INITIALIZER;

void errno_exit(const char *s) {
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

void *audio_disk_thread(void *arg) {
	int ret;
	DingleDots *dd = (DingleDots *)arg;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock(&dd->audio_thread_info.lock);
	while(1) {
		ret = write_audio_frame(dd, dd->video_output_context, &dd->audio_thread_info.stream);
		if (ret == 1) {
			dd->audio_done = 1;
			break;
		}
		if (ret == 0) continue;
		if (ret == -1) pthread_cond_wait(&dd->audio_thread_info.data_ready,
										 &dd->audio_thread_info.lock);
	}
	if (dd->audio_done && dd->video_done && !dd->trailer_written) {
		av_write_trailer(dd->video_output_context);
		dd->trailer_written = 1;
	}
	pthread_mutex_unlock(&dd->audio_thread_info.lock);
	return 0;
}

void *video_disk_thread (void *arg) {
	int ret;
	DingleDots *dd = (DingleDots *)arg;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock(&dd->video_thread_info.lock);
	while(1) {
		ret = write_video_frame(dd, dd->video_output_context,
								&dd->video_thread_info.stream);
		if (ret == 1) {
			dd->video_done = 1;
			break;
		}
		if (ret == 0) continue;
		if (ret == -1) pthread_cond_wait(&dd->video_thread_info.data_ready,
										 &dd->video_thread_info.lock);
	}
	if (dd->audio_done && dd->video_done && !dd->trailer_written) {
		av_write_trailer(dd->video_output_context);
		dd->trailer_written = 1;
	}
	pthread_mutex_unlock(&dd->video_thread_info.lock);
	printf("vid thread gets here\n");
	return 0;
}

int timespec2file_name(char *buf, uint len, char *dir, char *extension,
					   struct timespec *ts) {
	int ret;
	struct tm t;
	const char *homedir;
	if (localtime_r(&(ts->tv_sec), &t) == NULL) {
		return 1;
	}
	if ((homedir = getenv("HOME")) == NULL) {
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
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock(&dd->snapshot_thread_info.lock);
	frame = NULL;
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
	return 0;
}

ccv_tld_t *new_tld(int x, int y, int w, int h, DingleDots *dd) {
	ccv_tld_param_t p = ccv_tld_default_params;
	ccv_rect_t box = ccv_rect(x, y, w, h);
	ccv_read(dd->analysis_frame->data[0], &cdm, CCV_IO_ARGB_RAW | CCV_IO_GRAY,
			dd->analysis_rect.height, dd->analysis_rect.width, 4*dd->analysis_rect.width);
	return ccv_tld_new(cdm, box, p);
}

static void render_detection_box(cairo_t *cr, int initializing,
								 int x, int y, int w, int h) {
	double minimum = min(w, h);
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

static void render_selection_box(cairo_t *cr, DingleDots *dd) {
	cairo_save(cr);
	cairo_rectangle(cr, floor(dd->selection_rect.x)+0.5, floor(dd->selection_rect.y)+0.5,
					dd->selection_rect.width, dd->selection_rect.height);
	cairo_set_source_rgba(cr, 0.09, 0.28, 0.44, 0.15);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.15, 0.35, 0.68, 1);
	cairo_set_line_width(cr, 0.5);
	cairo_stroke(cr);
	cairo_restore(cr);
}

static int pair_int_int_cmp_z_order(const void *p1, const void *p2) {
	int ret;
	if (((const pair_int_int *)p1)->z > ((const pair_int_int *)p2)->z) ret = 1;
	if (((const pair_int_int*)p1)->z == ((const pair_int_int*)p2)->z) ret = 0;
	if (((const pair_int_int*)p1)->z < ((const pair_int_int*)p2)->z) ret = -1;
	return ret;
}

static int cmp_z_order(const void *p1, const void *p2) {
	int ret;
	if (((const Draggable *)p1)->z > ((const Draggable *)p2)->z) ret = 1;
	if (((const Draggable *)p1)->z == ((const Draggable *)p2)->z) ret = 0;
	if (((const Draggable *)p1)->z < ((const Draggable *)p2)->z) ret = -1;
	return ret;
}

static void render_pointer(cairo_t *cr, double x, double y) {
	double l = 10.;
	cairo_save(cr);
	cairo_set_source_rgba(cr, 1, 0, 0.1, 0.75);
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

void process_image(cairo_t *screen_cr, void *arg) {
	DingleDots *dd = (DingleDots *)arg;
	static int first_call = 1;
	static int first_data = 1;
	static struct timespec ts, snapshot_ts;
	static ccv_comp_t newbox;
	static int made_first_tld = 0;
	std::vector<Draggable *> draggables;
	int s, i,j,k;
	float sum, bw, bw2, diff, fr, fg, fb;
	int istart, jstart, iend, jend;
	int npts;
	uint32_t val;
	static uint32_t *save_buf;
	int do_tld;
	int render_drawing_surf = 0;
	cairo_t *sources_cr;
	cairo_surface_t *sources_surf;
	cairo_t *drawing_cr;
	cairo_surface_t *drawing_surf;
	do_tld = 0;
	if (first_data) {
		save_buf = (uint32_t *)malloc(dd->sources_frame->linesize[0] * dd->sources_frame->height);
	}
	if (!first_data && dd->doing_motion) {
		memcpy(save_buf, dd->sources_frame->data[0], 4 * dd->sources_frame->width *
				dd->sources_frame->height);
	}
	sources_surf = cairo_image_surface_create_for_data((unsigned char *)dd->sources_frame->data[0],
			CAIRO_FORMAT_ARGB32, dd->sources_frame->width, dd->sources_frame->height,
			dd->sources_frame->linesize[0]);
	sources_cr = cairo_create(sources_surf);
	drawing_surf = cairo_image_surface_create_for_data((unsigned char *)dd->drawing_frame->data[0],
			CAIRO_FORMAT_ARGB32, dd->drawing_frame->width, dd->drawing_frame->height,
			dd->drawing_frame->linesize[0]);
	drawing_cr = cairo_create(drawing_surf);
	cairo_save(screen_cr);
	cairo_scale(screen_cr, dd->scale, dd->scale);
	cairo_save(sources_cr);
	cairo_set_operator(sources_cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(sources_cr);
	cairo_restore(sources_cr);
	cairo_save(screen_cr);
	cairo_set_operator(screen_cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(screen_cr);
	cairo_restore(screen_cr);
	if (dd->do_snapshot || (dd->recording_started && !dd->recording_stopped)) {
		render_drawing_surf = 1;
	}
	for (i = 0; i < MAX_NUM_V4L2; i++) {
		if (dd->v4l2[i].active) {
			draggables.push_back(&dd->v4l2[i]);
		}
	}
	for (j = 0; j < MAX_NUM_VIDEO_FILES; j++) {
		if (dd->vf[j].active) {
			draggables.push_back(&dd->vf[j]);
		}
	}
	std::sort(draggables.begin(), draggables.end(), [](Draggable *a, Draggable *b) { return a->z < b->z; } );
	std::vector<cairo_t *> contexts;
	contexts.push_back(screen_cr);
	contexts.push_back(sources_cr);
	for (std::vector<Draggable *>::iterator it = draggables.begin(); it != draggables.end(); ++it) {
		(*it)->render(contexts);
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
	if (dd->doing_motion) {
		for (s = 0; s < MAX_NSOUND_SHAPES; s++) {
			if (!dd->sound_shapes[s].active) continue;
			sum = 0;
			npts = 0;
			istart = min(dd->drawing_rect.width, max(0, round(dd->sound_shapes[s].pos.x -
															  dd->sound_shapes[s].r)));
			jstart = min(dd->drawing_rect.height, max(0, round(dd->sound_shapes[s].pos.y -
															   dd->sound_shapes[s].r)));
			iend = max(0, min(dd->drawing_rect.width, round(dd->sound_shapes[s].pos.x +
															dd->sound_shapes[s].r)));
			jend = max(0, min(dd->drawing_rect.height, round(dd->sound_shapes[s].pos.y +
															 dd->sound_shapes[s].r)));
			for (i = istart; i < iend; i++) {
				for (j = jstart; j < jend; j++) {
					if (dd->sound_shapes[s].in(i, j)) {
						val = save_buf[i + j * dd->sources_frame->width];
						fr = ((val & 0x00ff0000) >> 16) / 256.;
						fg = ((val & 0x0000ff00) >> 8) / 256.;
						fb = ((val & 0x000000ff)) / 256.;
						bw = fr * 0.3 + fg * 0.59 + fb * 0.1;
						val = ((uint32_t *)dd->sources_frame->data[0])[i + j * dd->sources_frame->width];
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
			if ((sum / npts) > dd->motion_threshold) {
				dd->sound_shapes[s].set_motion_state(1);
			} else {
				dd->sound_shapes[s].set_motion_state(0);
			}
		}
	} else {
		for (s = 0; s < MAX_NSOUND_SHAPES; s++) {
			dd->sound_shapes[s].set_motion_state(0);
		}
	}
	for (s = 0; s < MAX_NSOUND_SHAPES; s++) {
		dd->sound_shapes[s].tick();
	}
	if (dd->doing_tld) {
		sws_scale(dd->analysis_resize, (uint8_t const * const *)dd->sources_frame->data,
				  dd->sources_frame->linesize, 0, dd->sources_frame->height, dd->analysis_frame->data, dd->analysis_frame->linesize);
		if (dd->make_new_tld == 1) {
			if (dd->user_tld_rect.width > 0 && dd->user_tld_rect.height > 0) {
				tld = new_tld(dd->user_tld_rect.x/dd->ascale_factor_x, dd->user_tld_rect.y/dd->ascale_factor_y,
							  dd->user_tld_rect.width/dd->ascale_factor_x, dd->user_tld_rect.height/dd->ascale_factor_y, dd);
			} else {
				tld = NULL;
				dd->doing_tld = 0;
				made_first_tld = 0;
				newbox.rect.x = 0;
				newbox.rect.y = 0;
				newbox.rect.width = 0;
				newbox.rect.height = 0;
				return;
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
			for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
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
		tld = NULL;
		made_first_tld = 0;
		newbox.rect.x = 0;
		newbox.rect.y = 0;
		newbox.rect.width = 0;
		newbox.rect.height = 0;
	}
	for (int i = 0; i < MAX_NSOUND_SHAPES; i++) {
		if (!dd->sound_shapes[i].active) continue;
		if (dd->sound_shapes[i].double_clicked_on || dd->sound_shapes[i].motion_state
				|| dd->sound_shapes[i].tld_state) {
			if (!dd->sound_shapes[i].on) {
				dd->sound_shapes[i].set_on();
			}
		}
		if (!dd->sound_shapes[i].double_clicked_on && !dd->sound_shapes[i].motion_state
				&& !dd->sound_shapes[i].tld_state) {
			if (dd->sound_shapes[i].on) {
				dd->sound_shapes[i].set_off();
			}
		}
	}
	isort(dd->sound_shapes, MAX_NSOUND_SHAPES, sizeof(SoundShape), cmp_z_order);
	std::vector<cairo_t *> ss_contexts;
	ss_contexts.push_back(screen_cr);
	for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
		if (!dd->sound_shapes[i].active) continue;
		if (render_drawing_surf) {
			ss_contexts.push_back(drawing_cr);
		}
		dd->sound_shapes[i].render(ss_contexts);
	}
#if defined(RENDER_KMETERS)
	for (i = 0; i < 2; i++) {
		kmeter_render(&dd->meters[i], drawing_cr, 1.);
		kmeter_render(&dd->meters[i], screen_cr, 1.);
	}
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
		if (render_drawing_surf) {
			render_selection_box(drawing_cr, dd);
		}
		render_selection_box(screen_cr, dd);
		for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
			if (!dd->sound_shapes[i].active) continue;
			GdkRectangle ss_rect;
			ss_rect.x = dd->sound_shapes[i].pos.x - dd->sound_shapes[i].r;
			ss_rect.y = dd->sound_shapes[i].pos.y - dd->sound_shapes[i].r;
			ss_rect.width = 2 * dd->sound_shapes[i].r;
			ss_rect.height = 2 * dd->sound_shapes[i].r;
			if (gdk_rectangle_intersect(&ss_rect, &dd->selection_rect, NULL)) {
				dd->sound_shapes[i].selected = 1;
				dd->sound_shapes[i].selected_pos.x = dd->sound_shapes[i].pos.x;
				dd->sound_shapes[i].selected_pos.y = dd->sound_shapes[i].pos.y;
			} else {
				dd->sound_shapes[i].selected = 0;
			}
		}
	}
	if (render_drawing_surf) {
		render_pointer(drawing_cr, dd->mouse_pos.x, dd->mouse_pos.y);
	}
	render_pointer(screen_cr, dd->mouse_pos.x, dd->mouse_pos.y);
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
	cairo_restore(screen_cr);
	/*cairo_save(cr);
	cairo_scale(cr, dd->scale, dd->scale);
	cairo_set_source_surface(cr, drawing_surf, 0.0, 0.0);
	cairo_paint(cr);*/
	/*cairo_destroy(drawing_cr);
	cairo_surface_destroy(drawing_surf);*/
	/*sws_scale(dd->screen_resize, (const uint8_t* const*)dd->drawing_frame->data,
	 dd->drawing_frame->linesize, 0, dd->drawing_frame->height, dd->screen_frame->data,
	 dd->screen_frame->linesize);
  cairo_surface_t *screen_surf;
  screen_surf = cairo_image_surface_create_for_data((unsigned char *)dd->screen_frame->data[0],
   CAIRO_FORMAT_ARGB32, dd->screen_frame->width, dd->screen_frame->height,
	 dd->screen_frame->linesize[0]);
	cairo_set_source_surface(cr, screen_surf, 0.0, 0.0);*/
	cairo_destroy(sources_cr);
	cairo_destroy(drawing_cr);
	cairo_surface_destroy(sources_surf);
	cairo_surface_destroy(drawing_surf);
	clock_gettime(CLOCK_REALTIME, &snapshot_ts);
	int drawing_size = 4 * dd->drawing_frame->width * dd->drawing_frame->height;
	int tsize = drawing_size + sizeof(struct timespec);
	if (dd->do_snapshot) {
		if (jack_ringbuffer_write_space(dd->snapshot_thread_info.ring_buf) >= tsize) {
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
	if (dd->recording_stopped) {
		if (pthread_mutex_trylock(&dd->video_thread_info.lock) == 0) {
			pthread_cond_signal(&dd->video_thread_info.data_ready);
			pthread_mutex_unlock(&dd->video_thread_info.lock);
		}
	}
	if (!dd->recording_started || dd->recording_stopped) return;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	if (first_call) {
		first_call = 0;
		dd->video_thread_info.stream.first_time = ts;
		dd->can_capture = 1;
	}
	tsize = drawing_size + sizeof(struct timespec);
	if (jack_ringbuffer_write_space(video_ring_buf) >= tsize) {
		jack_ringbuffer_write(video_ring_buf, (const char *)dd->drawing_frame->data[0],
				drawing_size);
		jack_ringbuffer_write(video_ring_buf, (const char *)&ts,
							  sizeof(struct timespec));
		if (pthread_mutex_trylock(&dd->video_thread_info.lock) == 0) {
			pthread_cond_signal(&dd->video_thread_info.data_ready);
			pthread_mutex_unlock(&dd->video_thread_info.lock);
		}
	}
}

double hanning_window(int i, int N) {
	return 0.5 * (1 - cos(2 * M_PI * i / (N - 1)));
}

int process(jack_nframes_t nframes, void *arg) {
	DingleDots *dd = (DingleDots *)arg;
	int chn;
	size_t i;
	static int first_call = 1;
	if (!dd->can_process) return 0;
	midi_process_output(nframes, dd);
	for (chn = 0; chn < dd->nports; chn++) {
		dd->in[chn] = (jack_default_audio_sample_t *)jack_port_get_buffer(dd->in_ports[chn], nframes);
		dd->out[chn] = (jack_default_audio_sample_t *)jack_port_get_buffer(dd->out_ports[chn], nframes);
		kmeter_process(&dd->meters[chn], dd->in[chn], nframes);
	}
	if (nframes >= FFT_SIZE) {
		for (i = 0; i < FFT_SIZE; i++) {
			fftw_in[i][0] = dd->in[0][i] * hanning_window(i, FFT_SIZE);
			fftw_in[i][1] = 0.0;
		}
		fftw_execute(p);
	}
	if (first_call) {
		struct timespec *ats = &dd->audio_thread_info.stream.first_time;
		clock_gettime(CLOCK_MONOTONIC, ats);
		dd->audio_thread_info.stream.samples_count = 0;
		first_call = 0;
	}
	for (i = 0; i < nframes; i++) {
		for (chn = 0; chn < dd->nports; chn++) {
			dd->out[chn][i] = dd->in[chn][i];
		}
	}
	if (dd->recording_started && !dd->audio_done) {
		for (i = 0; i < nframes; i++) {
			for (chn = 0; chn < dd->nports; chn++) {
				if (jack_ringbuffer_write (audio_ring_buf, (const char *) (dd->in[chn]+i),
										   sample_size) < sample_size) {
					printf("jack overrun: %ld\n", ++dd->jack_overruns);
				}
			}
		}
		if (pthread_mutex_trylock (&dd->audio_thread_info.lock) == 0) {
			pthread_cond_signal (&dd->audio_thread_info.data_ready);
			pthread_mutex_unlock (&dd->audio_thread_info.lock);
		}
	}
	return 0;
}

void jack_shutdown (void *arg) {
	printf("JACK shutdown\n");
	abort();
}

void setup_jack(DingleDots *dd) {
	unsigned int i;
	size_t in_size;
	dd->can_process = 0;
	dd->jack_overruns = 0;
	if ((dd->client = jack_client_open("v4l2_wayland",
									   JackNoStartServer, NULL)) == 0) {
		printf("jack server not running?\n");
		exit(1);
	}
	jack_set_process_callback(dd->client, process, dd);
	jack_on_shutdown(dd->client, jack_shutdown, NULL);
	if (jack_activate(dd->client)) {
		printf("cannot activate jack client\n");
	}
	dd->in_ports = (jack_port_t **) malloc(sizeof(jack_port_t *) * dd->nports);
	dd->out_ports = (jack_port_t **) malloc(sizeof(jack_port_t *) * dd->nports);
	in_size =  dd->nports * sizeof (jack_default_audio_sample_t *);
	dd->in = (jack_default_audio_sample_t **) malloc (in_size);
	dd->out = (jack_default_audio_sample_t **) malloc (in_size);
	audio_ring_buf = jack_ringbuffer_create (dd->nports * sample_size *
											 16384);
	memset(dd->in, 0, in_size);
	memset(dd->out, 0, in_size);
	memset(audio_ring_buf->buf, 0, audio_ring_buf->size);
	dd->midi_ring_buf = jack_ringbuffer_create(MIDI_RB_SIZE);
	fftw_in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
	fftw_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
	p = fftw_plan_dft_1d(FFT_SIZE, fftw_in, fftw_out, FFTW_FORWARD, FFTW_ESTIMATE);
	for (i = 0; i < dd->nports; i++) {
		char name[64];
		sprintf(name, "input%d", i + 1);
		if ((dd->in_ports[i] = jack_port_register (dd->client, name, JACK_DEFAULT_AUDIO_TYPE,
												   JackPortIsInput, 0)) == 0) {
			printf("cannot register input port \"%s\"!\n", name);
			jack_client_close(dd->client);
			exit(1);
		}
		sprintf(name, "output%d", i + 1);
		if ((dd->out_ports[i] = jack_port_register (dd->client, name, JACK_DEFAULT_AUDIO_TYPE,
													JackPortIsOutput, 0)) == 0) {
			printf("cannot register output port \"%s\"!\n", name);
			jack_client_close(dd->client);
			exit(1);
		}
	}
	dd->midi_port = jack_port_register(dd->client, "output_midi",
									   JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	dd->can_process = 1;
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

void start_recording(DingleDots *dd) {
	struct timespec ts;
	if (strlen(dd->video_file_name) == 0) {
		clock_gettime(CLOCK_REALTIME, &ts);
		timespec2file_name(dd->video_file_name, STR_LEN, "Videos", "webm", &ts);
	}
	init_output(dd);
	dd->recording_started = 1;
	dd->recording_stopped = 0;
	pthread_create(&dd->audio_thread_info.thread_id, NULL, audio_disk_thread,
				   dd);
	pthread_create(&dd->video_thread_info.thread_id, NULL, video_disk_thread,
				   dd);
}

void stop_recording(DingleDots *dd) {
	dd->recording_stopped = 1;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->record_button), 0);
	gtk_widget_set_sensitive(dd->record_button, 0);
}

static gboolean configure_event_cb (GtkWidget *widget,
									GdkEventConfigure *event, gpointer data) {
	int ret;
	DingleDots *dd;
	dd = (DingleDots *)data;
	dd->scale = (double)(event->height) / dd->drawing_rect.height;
	printf("scale %f\n", dd->scale);
	if (dd->screen_frame) {
		av_freep(&dd->screen_frame->data[0]);
		av_frame_free(&dd->screen_frame);
	}
	dd->screen_frame = av_frame_alloc();
	dd->screen_frame->format = AV_PIX_FMT_ARGB;
	dd->screen_frame->width = event->width;
	dd->screen_frame->height = event->height;
	ret = av_image_alloc(dd->screen_frame->data, dd->screen_frame->linesize,
						 dd->screen_frame->width, dd->screen_frame->height,
						 (AVPixelFormat)dd->screen_frame->format, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate raw picture buffer\n");
		exit(1);
	}
	/* We've handled the configure event, no need for further processing.
   * */
	return TRUE;
}

static gboolean window_state_event_cb (GtkWidget *widget,
									   GdkEventWindowState *event, gpointer   data) {
	DingleDots *dd;
	dd = (DingleDots *)data;
	if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) {
		dd->fullscreen = TRUE;
	} else {
		dd->fullscreen = FALSE;
	}
	return FALSE;
}

static gboolean draw_cb (GtkWidget *widget, cairo_t *cr, gpointer   data)
{
	DingleDots *dd;
	dd = (DingleDots *)data;
	process_image(cr, dd);
	return FALSE;
}

static gboolean motion_notify_event_cb(GtkWidget *widget,
									   GdkEventMotion *event, gpointer data) {
	int i;
	DingleDots *dd = (DingleDots *)data;
	gtk_widget_queue_draw(dd->drawing_area);
	dd->mouse_pos.x = event->x * dd->drawing_rect.width / dd->screen_frame->width;
	dd->mouse_pos.y = event->y * dd->drawing_rect.height / dd->screen_frame->height;
	for (i = 0; i < MAX_NSOUND_SHAPES;i++) {
		if (!dd->sound_shapes[i].active) continue;
		dd->sound_shapes[i].hovered = 0;
	}
	for (i = MAX_NSOUND_SHAPES-1; i >= 0; i--) {
		SoundShape *s = &dd->sound_shapes[i];
		if (!s->active) continue;
		if (s->in(dd->mouse_pos.x, dd->mouse_pos.y)) {
			s->hovered = 1;
			break;
		}
	}
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
		dd->selection_rect.width = dd->mouse_pos.x - dd->mdown_pos.x;
		dd->selection_rect.height = dd->mouse_pos.y - dd->mdown_pos.y;
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
	isort(dd->sound_shapes, MAX_NSOUND_SHAPES, sizeof(SoundShape), cmp_z_order);
	for (i = MAX_NSOUND_SHAPES-1; i > -1; i--) {
		if (!dd->sound_shapes[i].active) continue;
		if (dd->sound_shapes[i].mdown) {
			if (dd->sound_shapes[i].selected) {
				for (int j = 0; j < MAX_NSOUND_SHAPES; j++) {
					if (!dd->sound_shapes[j].active) continue;
					if (dd->sound_shapes[j].selected) {
						dd->sound_shapes[j].pos.x = dd->mouse_pos.x -
								dd->sound_shapes[i].mdown_pos.x +
								dd->sound_shapes[j].selected_pos.x;
						dd->sound_shapes[j].pos.y = dd->mouse_pos.y -
								dd->sound_shapes[i].mdown_pos.y +
								dd->sound_shapes[j].selected_pos.y;
					}
				}
				return TRUE;
			} else {
				dd->sound_shapes[i].drag(dd->mouse_pos.x, dd->mouse_pos.y);
				return TRUE;
			}
		}
	}
	for (i = MAX_NUM_V4L2-1; i > -1; i--) {
		if (!dd->v4l2[i].active) continue;
		if (dd->v4l2[i].mdown) {
			dd->v4l2[i].drag(dd->mouse_pos.x, dd->mouse_pos.y);
			return TRUE;
		}
	}
	for (i = MAX_NUM_VIDEO_FILES-1; i > -1; i--) {
		if (!dd->vf[i].active) continue;
		if (dd->vf[i].mdown) {
			dd->vf[i].drag(dd->mouse_pos.x, dd->mouse_pos.y);
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean double_press_event_cb(GtkWidget *widget,
									  GdkEventButton *event, gpointer data) {
	DingleDots * dd = (DingleDots *)data;
	if (event->type == GDK_2BUTTON_PRESS &&
			event->button == GDK_BUTTON_PRIMARY) {
		uint8_t found = 0;
		isort(dd->sound_shapes, MAX_NSOUND_SHAPES, sizeof(SoundShape), cmp_z_order);
		for (int i = MAX_NSOUND_SHAPES-1; i > -1; i--) {
			if (!dd->sound_shapes[i].active) continue;
			double x, y;
			x = event->x * dd->drawing_rect.width / dd->screen_frame->width;
			y = event->y * dd->drawing_rect.height / dd->screen_frame->height;
			if (dd->sound_shapes[i].in(x, y)) {
				found = 1;
				if (dd->sound_shapes[i].double_clicked_on) {
					dd->sound_shapes[i].double_clicked_on = 0;
				} else {
					dd->sound_shapes[i].double_clicked_on = 1;
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

static gboolean button_press_event_cb(GtkWidget *widget,
									  GdkEventButton *event, gpointer data) {
	int i;
	DingleDots * dd = (DingleDots *)data;
	dd->mouse_pos.x = event->x * dd->drawing_rect.width / dd->screen_frame->width;
	dd->mouse_pos.y = event->y * dd->drawing_rect.height / dd->screen_frame->height;
	if (event->button == GDK_BUTTON_PRIMARY) {
		dd->mdown = 1;
		dd->mdown_pos.x = dd->mouse_pos.x;
		dd->mdown_pos.y = dd->mouse_pos.y;
		isort(dd->sound_shapes, MAX_NSOUND_SHAPES,
			  sizeof(SoundShape), cmp_z_order);
		for (i = MAX_NSOUND_SHAPES-1; i > -1; i--) {
			if (!dd->sound_shapes[i].active) continue;
			if (dd->sound_shapes[i].in(dd->mouse_pos.x, dd->mouse_pos.y)) {
				if (dd->delete_active) {
					dd->sound_shapes[i].deactivate();
				} else {
					if (!dd->sound_shapes[i].selected) {
						for (int j = 0; j < MAX_NSOUND_SHAPES; j++) {
							if (!dd->sound_shapes[j].active) continue;
							dd->sound_shapes[j].selected = 0;
						}
						dd->sound_shapes[i].selected = 0;
					}
					if (dd->sound_shapes[i].selected) {
						for (int j = 0; j < MAX_NSOUND_SHAPES; j++) {
							if (!dd->sound_shapes[j].active) continue;
							if (dd->sound_shapes[j].selected) {
								dd->sound_shapes[j].z = dd->next_z++;
								dd->sound_shapes[j].selected_pos.x = dd->sound_shapes[j].pos.x;
								dd->sound_shapes[j].selected_pos.y = dd->sound_shapes[j].pos.y;
							}
						}
					}
					dd->sound_shapes[i].set_mdown(dd->mouse_pos.x,
												  dd->mouse_pos.y, dd->next_z++);
				}
				return FALSE;
			}
		}
		if (event->state & GDK_CONTROL_MASK) {
			std::vector<Draggable *> draggables;
			for (i = MAX_NUM_V4L2-1; i > -1; i--) {
				if (!dd->v4l2[i].active) continue;
				if (dd->v4l2[i].in(dd->mouse_pos.x, dd->mouse_pos.y)) {
					draggables.push_back(&dd->v4l2[i]);
				}
			}
			for (i = MAX_NUM_VIDEO_FILES-1; i > -1; i--) {
				if (!dd->vf[i].active) continue;
				if (dd->vf[i].in(dd->mouse_pos.x, dd->mouse_pos.y)) {
					draggables.push_back(&dd->vf[i]);
				}
			}
			std::sort(draggables.begin(), draggables.end(), [](Draggable *a, Draggable *b) { return a->z > b->z; } );
			for (std::vector<Draggable *>::iterator it = draggables.begin(); it != draggables.end(); ++it) {
				if ((*it)->in(dd->mouse_pos.x, dd->mouse_pos.y)) {
					(*it)->set_mdown(dd->mouse_pos.x, dd->mouse_pos.y,
									 dd->next_z++);
					break;
				}
			}
			return false;
		}
		dd->selection_in_progress = 1;
		dd->selection_rect.x = dd->mouse_pos.x;
		dd->selection_rect.y = dd->mouse_pos.y;
		dd->selection_rect.width = 0;
		dd->selection_rect.height = 0;
		return FALSE;
	}
	if (!dd->shift_pressed && event->button == GDK_BUTTON_SECONDARY) {
		dd->smdown = 1;
		dd->mdown_pos.x = dd->mouse_pos.x;
		dd->mdown_pos.y = dd->mouse_pos.y;
		dd->user_tld_rect.x = dd->mdown_pos.x;
		dd->user_tld_rect.y = dd->mdown_pos.y;
		dd->user_tld_rect.width = 20 * dd->ascale_factor_x;
		dd->user_tld_rect.height = 20 * dd->ascale_factor_y;
		return TRUE;
	}
	if (dd->doing_tld && dd->shift_pressed) {
		if (event->button == GDK_BUTTON_SECONDARY) {
			dd->doing_tld = 0;
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean button_release_event_cb(GtkWidget *widget,
										GdkEventButton *event, gpointer data) {
	DingleDots * dd;
	int i;
	dd = (DingleDots *)data;
	if (event->button == GDK_BUTTON_PRIMARY) {
		if (!dd->dragging && !dd->selection_in_progress) {
			for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
				if (!dd->sound_shapes[i].active) continue;
				if (!dd->sound_shapes[i].mdown) {
					dd->sound_shapes[i].selected = 0;
				}
			}
		}
		for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
			if (!dd->sound_shapes[i].active) continue;
			dd->sound_shapes[i].mdown = 0;
		}
		for (i = 0; i < MAX_NUM_V4L2; i++) {
			if (!dd->v4l2[i].active) continue;
			dd->v4l2[i].mdown = 0;
		}
		for (i = 0; i < MAX_NUM_VIDEO_FILES; i++) {
			if (!dd->vf[i].active) continue;
			dd->vf[i].mdown = 0;
		}
		dd->mdown = 0;
		dd->dragging = 0;
		dd->selection_in_progress = 0;
		return TRUE;
	} else if (!dd->shift_pressed && event->button == GDK_BUTTON_SECONDARY) {
		dd->smdown = 0;
		dd->make_new_tld = 1;
		dd->doing_tld = 1;
		return TRUE;
	}
	return FALSE;
}

static gboolean scroll_cb(GtkWidget *widget, GdkEventScroll *event,
						  gpointer data) {
	DingleDots *dd = (DingleDots *)data;
	int i;
	if (event->state & GDK_CONTROL_MASK) {
		gboolean up = FALSE;
		double inc = 2 * M_PI / 160;
		if (event->delta_y == -1.0) {
			up = TRUE;
		}
		for (i = MAX_NUM_V4L2-1; i > -1; i--) {
			if (!dd->v4l2[i].active) continue;
			if (dd->v4l2[i].in(dd->mouse_pos.x, dd->mouse_pos.y)) {
				if (up) {
					dd->v4l2[i].rotation_radians += inc;
				} else {
					dd->v4l2[i].rotation_radians -= inc;
				}
				return FALSE;
			}
		}
		for (i = MAX_NUM_VIDEO_FILES-1; i > -1; i--) {
			if (!dd->vf[i].active) continue;
			if (dd->vf[i].in(dd->mouse_pos.x, dd->mouse_pos.y)) {
				if (up) {
					dd->vf[i].rotation_radians += inc;
				} else {
					dd->vf[i].rotation_radians -= inc;
				}
				return FALSE;
			}
		}
	}
	return TRUE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
							 gpointer data) {
	DingleDots *dd = (DingleDots *)data;
	if (event->keyval == GDK_KEY_q && (!dd->recording_started ||
									   dd->recording_stopped)) {
		g_application_quit(dd->app);
	} else if (event->keyval == GDK_KEY_r && (!dd->recording_started)) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->record_button), 1);
		return TRUE;
	} else if (event->keyval == GDK_KEY_r && (dd->recording_started)) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->record_button), 0);
		return TRUE;
	} else if (event->keyval == GDK_KEY_d) {
		if (!dd->delete_active) {
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->delete_button), 1);
		} else {
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dd->delete_button), 0);
		}
	} else if (event->keyval == GDK_KEY_Shift_L ||
			   event->keyval == GDK_KEY_Shift_R) {
		dd->shift_pressed = 1;
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

static gboolean on_key_release(GtkWidget *widget, GdkEventKey *event,
							   gpointer data) {
	DingleDots *dd = (DingleDots *)data;
	if (event->keyval == GDK_KEY_Shift_L ||
			event->keyval == GDK_KEY_Shift_R) {
		dd->shift_pressed = 0;
		return TRUE;
	}
	return FALSE;
}

static gboolean delete_cb(GtkWidget *widget, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	if (!dd->delete_active) {
		dd->delete_active = 1;
	} else {
		dd->delete_active = 0;
	}
	return TRUE;
}

static gboolean record_cb(GtkWidget *widget, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	if (!dd->recording_started && !dd->recording_stopped) {
		start_recording(dd);
	} else if (dd->recording_started && !dd->recording_stopped) {
		stop_recording(dd);
	}
	return TRUE;
}

static gboolean quit_cb(GtkWidget *widget, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	g_application_quit(dd->app);
	return TRUE;
}

static gboolean play_file_cb(GtkWidget *widget, gpointer data) {
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
		int index = dd->current_video_file_source_index++ % MAX_NUM_VIDEO_FILES;
		if(dd->vf[index].allocated) {
			printf("video_file %d alloced\n", index);
			dd->vf[index].destroy();
		}
		dd->vf[index].dd = dd;
		dd->vf[index].create(dd, filename);
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

static gboolean camera_cb(GtkWidget *widget, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	GtkWidget *dialog;
	GtkWidget *dialog_content;
	GtkWidget *combo;
	int res;
	GtkDialogFlags flags = (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT);
	dialog = gtk_dialog_new_with_buttons("Open Camera", GTK_WINDOW(dd->ctl_window),
										 flags, "Open", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_REJECT, NULL);
	dialog_content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "0", "/dev/video0");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "1", "/dev/video1");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "2", "/dev/video2");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "3", "/dev/video3");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
	gtk_container_add(GTK_CONTAINER(dialog_content), combo);
	gtk_widget_show_all(dialog);
	res = gtk_dialog_run(GTK_DIALOG(dialog));
	if (res == GTK_RESPONSE_ACCEPT) {
		gchar *name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
		for(int i = 0; i < MAX_NUM_V4L2; i++) {
			if (!dd->v4l2[i].active) {
				dd->v4l2[i].create(dd, name, dd->drawing_frame->width, dd->drawing_frame->height, dd->next_z++);
				break;
			}
		}
	}
	gtk_widget_destroy (dialog);
	return TRUE;
}

static gboolean snapshot_cb(GtkWidget *widget, gpointer data) {
	DingleDots * dd;
	dd = (DingleDots *)data;
	dd->do_snapshot = 1;
	return TRUE;
}

static gboolean make_scale_cb(GtkWidget *widget, gpointer data) {
	DingleDots * dd;
	GdkRGBA gc;
	struct hsva h;
	color c;
	dd = (DingleDots *)data;
	char *text_scale;
	char text_note[4];
	text_scale =
			gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dd->scale_combo));
	sprintf(text_note, "%s",
			gtk_combo_box_get_active_id(GTK_COMBO_BOX(dd->note_combo)));
	midi_key_t key;
	midi_key_init_by_scale_id(&key, atoi(text_note),
							  midi_scale_text_to_id(text_scale));
	if (dd->use_rand_color_for_scale) {
		h.h = (double) rand() / RAND_MAX;
		h.v = 0.45;
		h.s = 1.0;
		h.a = 0.5;
		c = hsv2rgb(&h);
	} else {
		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dd->scale_color_button), &gc);
		color_init(&c, gc.red, gc.green, gc.blue, gc.alpha);
	}
	dd->add_scale(&key, 1, &c);
	return TRUE;
}

static void activate(GtkApplication *app, gpointer user_data) {
	GtkWidget *window;
	GtkWidget *drawing_area;
	GtkWidget *note_hbox;
	GtkWidget *vbox;
	GtkWidget *qbutton;
	GtkWidget *mbutton;
	GtkWidget *play_file_button;
	GtkWidget *snapshot_button;
	GtkWidget *camera_button;
	GtkWidget *make_scale_button;
	GtkWidget *aspect;
	DingleDots *dd;
	int ret;
	dd = (DingleDots *)user_data;
	ccv_enable_default_cache();
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
	uint32_t rb_size = 5 * 4 * dd->drawing_frame->linesize[0] *
			dd->drawing_frame->height;
	video_ring_buf = jack_ringbuffer_create(rb_size);
	memset(video_ring_buf->buf, 0, video_ring_buf->size);
	dd->snapshot_thread_info.ring_buf = jack_ringbuffer_create(rb_size);
	memset(dd->snapshot_thread_info.ring_buf->buf, 0,
		   dd->snapshot_thread_info.ring_buf->size);
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
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	dd->record_button = gtk_toggle_button_new_with_label("RECORD");
	dd->delete_button = gtk_toggle_button_new_with_label("DELETE");
	qbutton = gtk_button_new_with_label("QUIT");
	mbutton = gtk_check_button_new_with_label("MOTION DETECTION");
	play_file_button = gtk_button_new_with_label("PLAY VIDEO FILE");
	snapshot_button = gtk_button_new_with_label("TAKE SNAPSHOT");
	camera_button = gtk_button_new_with_label("OPEN CAMERA");
	gtk_box_pack_start(GTK_BOX(vbox), dd->record_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), snapshot_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), mbutton, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), play_file_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), camera_button, FALSE, FALSE, 0);
	dd->scale_combo = gtk_combo_box_text_new();
	int i = 0;
	char *name = midi_scale_id_to_text(i);
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
	gtk_box_pack_start(GTK_BOX(note_hbox), make_scale_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), dd->delete_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), qbutton, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER (window), aspect);
	gtk_container_add (GTK_CONTAINER (dd->ctl_window), vbox);

	g_signal_connect(dd->record_button, "clicked", G_CALLBACK(record_cb), dd);
	g_signal_connect(dd->delete_button, "clicked", G_CALLBACK(delete_cb), dd);
	g_signal_connect(qbutton, "clicked", G_CALLBACK(quit_cb), dd);
	g_signal_connect(snapshot_button, "clicked", G_CALLBACK(snapshot_cb), dd);
	g_signal_connect(camera_button, "clicked", G_CALLBACK(camera_cb), dd);
	g_signal_connect(make_scale_button, "clicked", G_CALLBACK(make_scale_cb), dd);
	g_signal_connect(mbutton, "toggled", G_CALLBACK(motion_cb), dd);
	g_signal_connect(play_file_button, "clicked", G_CALLBACK(play_file_cb), dd);
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

static void isort(void *base, size_t nmemb, size_t size,
				  int (*compar)(const void *, const void *)) {
	int c, d;
	char temp[size];
	for (c = 1 ; c <= nmemb - 1; c++) {
		d = c;
		while ( d > 0 && (compar((void *)((char *)base)+d*size,
								 (void *)((char *)base)+(d-1)*size) < 0)) {
			memcpy(temp, ((char *)base)+d*size, size);
			memcpy(((char *)base)+d*size, ((char *)base)+(d-1)*size, size);
			memcpy(((char *)base)+(d-1)*size, temp, size);
			d--;
		}
	}
}

static void signal_handler(int sig) {
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

void setup_signal_handler() {
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
}

static void usage(DingleDots *dd, FILE *fp, int argc, char **argv)
{
	fprintf(fp,
			"Usage: %s [options]\n\n"
			"Options:\n"
			"-d | --device name   Video device name\n"
			"-h | --help          Print this message\n"
			"-o | --output        filename of video file output\n"
			"-b | --bitrate       bit rate of video file output\n"
			"",
			argv[0]);
}

static const char short_options[] = "d:ho:b:w:g:x:y:";

static const struct option
		long_options[] = {
{ "device", required_argument, NULL, 'd' },
{ "help",   no_argument,       NULL, 'h' },
{ "output", required_argument, NULL, 'o' },
{ "bitrate", required_argument, NULL, 'b' },
{ "width", required_argument, NULL, 'w' },
{ "height", required_argument, NULL, 'g' },
{ "tld_width", required_argument, NULL, 'x' },
{ "tld_height", required_argument, NULL, 'y' },
{ 0, 0, 0, 0 }
};

int main(int argc, char **argv) {
	DingleDots dingle_dots;
	char *video_file_name;
	int width = 1280;
	int height = 720;
	int video_bitrate = 1000000;
	char *dev_name = "/dev/video0";
	srand(time(NULL));
	video_file_name = "";
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
			case 'd':
				dev_name = optarg;
				break;
			case 'o':
				video_file_name = optarg;
				break;
			case 'b':
				video_bitrate = atoi(optarg);
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
	dingle_dots.init(dev_name, width, height, video_file_name, video_bitrate);
	setup_jack(&dingle_dots);
	setup_signal_handler();
	mainloop(&dingle_dots);
	dingle_dots.deactivate_sound_shapes();
	dingle_dots.free();
	teardown_jack(&dingle_dots);
	fprintf(stderr, "\n");
	return 0;
}
