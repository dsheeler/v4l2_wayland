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
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>
#include <linux/input.h>
#include <ccv/ccv.h>
#include <cairo/cairo.h>
#include <linux/videodev2.h>
#include <fftw3.h>

#include "dingle_dots.h"
#include "kmeter.h"
#include "muxing.h"
#include "sound_shape.h"
#include "midi.h"
#include "v4l2_wayland.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define CLIPVALUE(v) ((v) < 255 ? (v) : 255)

struct buffer {
  void   *start;
  size_t  length;
};

#define FFT_SIZE 256
#define MIDI_RB_SIZE 1024 * sizeof(struct midi_message)
#define STR_LEN 80

static int read_frame(cairo_t *cr, dingle_dots_t *dd);
static void isort(void *base, size_t nmemb, size_t size,
 int (*compar)(const void *, const void *));

fftw_complex                   *fftw_in, *fftw_out;
fftw_plan                      p;
AVFormatContext                *oc;
char                           *out_file_name;
uint32_t                       stream_bitrate = 800000;
static ccv_dense_matrix_t      *cdm = 0, *cdm2 = 0;
static ccv_tld_t               *tld = 0;
int                             recording_started = 0;
int                             recording_stopped = 0;
static int                      audio_done = 0, video_done = 0,
                                trailer_written = 0;
static int                      shift_pressed = 0;

static int                make_new_tld = 0;
static char              *dev_name;
static int                fd = -1;
struct buffer            *buffers;
static unsigned int       n_buffers;

volatile int              can_process;
volatile int              can_capture;
jack_ringbuffer_t        *video_ring_buf, *audio_ring_buf;
unsigned int              nports = 2;
jack_port_t             **in_ports, **out_ports;
jack_default_audio_sample_t **in;
jack_nframes_t            nframes;
const size_t              sample_size = sizeof(jack_default_audio_sample_t);
pthread_mutex_t           av_thread_lock = PTHREAD_MUTEX_INITIALIZER;
long                      jack_overruns = 0;

static void errno_exit(const char *s)
{
  fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
  exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg)
{
  int r;
  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);
  return r;
}

void *audio_disk_thread(void *arg) {
  int ret;
  dingle_dots_t *dd = (dingle_dots_t *)arg;
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  pthread_mutex_lock(&dd->audio_thread_info.lock);
  while(1) {
    ret = write_audio_frame(dd, oc, &dd->audio_thread_info.stream);
    if (ret == 1) {
      audio_done = 1;
      break;
    }
    if (ret == 0) continue;
    if (ret == -1) pthread_cond_wait(&dd->audio_thread_info.data_ready,
     &dd->audio_thread_info.lock);
  }
  if (audio_done && video_done && !trailer_written) {
    av_write_trailer(oc);
    trailer_written = 1;
  }
  pthread_mutex_unlock(&dd->audio_thread_info.lock);
	return 0;
}

void *video_disk_thread (void *arg) {
  int ret;
  dingle_dots_t *dd = (dingle_dots_t *)arg;
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  pthread_mutex_lock(&dd->video_thread_info.lock);
  while(1) {
    ret = write_video_frame(oc, &dd->video_thread_info.stream);
    if (ret == 1) {
      video_done = 1;
      break;
    }
    if (ret == 0) continue;
    if (ret == -1) pthread_cond_wait(&dd->video_thread_info.data_ready,
     &dd->video_thread_info.lock);
  }
  if (audio_done && video_done && !trailer_written) {
    av_write_trailer(oc);
    trailer_written = 1;
  }
  pthread_mutex_unlock(&dd->video_thread_info.lock);
	return 0;
}

int timespec2snapshot_name(char *buf, uint len, struct timespec *ts) {
	int ret;
	struct tm t;
	const char *homedir;
	if (localtime_r(&(ts->tv_sec), &t) == NULL) {
		return 1;
	}
  if ((homedir = getenv("HOME")) == NULL) {
		homedir = getpwuid(getuid())->pw_dir;
	}
	ret = snprintf(buf, len, "%s/Pictures/v4l2_wayland-snapshot-", homedir);
	len -= ret - 1;
	ret = strftime(&buf[strlen(buf)], len, "%Y-%m-%d-%H:%M:%S", &t);
	if (ret == 0)
		return 2;
	len -= ret - 1;
	ret = snprintf(&buf[strlen(buf)], len, ".%03d.png",
	 (int)ts->tv_nsec/1000000);
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
  dingle_dots_t *dd = (dingle_dots_t *)arg;
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  pthread_mutex_lock(&dd->snapshot_thread_info.lock);
  frame = NULL;
  frame = av_frame_alloc();
  frame->width = dd->camera_rect.width;
  frame->height = dd->camera_rect.height;
  frame->format = AV_PIX_FMT_ARGB;
  av_image_alloc(frame->data, frame->linesize,
   frame->width, frame->height, frame->format, 1);
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
  		timespec2snapshot_name(timestr, STR_LEN, &ts);
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

static void YUV2RGB(const unsigned char y, const unsigned char u,const unsigned char v, unsigned char* r,
                    unsigned char* g, unsigned char* b)
{
  const int y2 = (int)y;
  const int u2 = (int)u - 128;
  const int v2 = (int)v - 128;
  // This is the normal YUV conversion, but
  // appears to be incorrect for the firewire cameras
  //   int r2 = y2 + ( (v2*91947) >> 16);
  //   int g2 = y2 - ( ((u2*22544) + (v2*46793)) >> 16 );
  //   int b2 = y2 + ( (u2*115999) >> 16);
  // This is an adjusted version (UV spread out a bit)
  int r2 = y2 + ((v2 * 37221) >> 15);
  int g2 = y2 - (((u2 * 12975) + (v2 *
          18949)) >> 15);
  int b2 = y2 + ((u2 * 66883) >> 15);
  // Cap the values.
  *r = CLIPVALUE(r2);
  *g = CLIPVALUE(g2);
  *b = CLIPVALUE(b2);
}

ccv_tld_t *new_tld(int x, int y, int w, int h, dingle_dots_t *dd) {
  ccv_tld_param_t p = ccv_tld_default_params;
  ccv_rect_t box = ccv_rect(x, y, w, h);
  ccv_read(dd->analysis_frame->data[0], &cdm, CCV_IO_ARGB_RAW | CCV_IO_GRAY,
	 dd->analysis_rect.height, dd->analysis_rect.width, 4*dd->analysis_rect.width);
  return ccv_tld_new(cdm, box, p);
}

static void render_detection_box(cairo_t *cr, int initializing,
 int x, int y, int w, int h) {
  double minimum = min(w, h);
  double r = 0.1 * minimum;
  cairo_save(cr);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
  cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2. * M_PI);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * M_PI);
  cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
  cairo_close_path(cr);
  if (initializing) cairo_set_source_rgba(cr, 0.85, 0.85, 0., 0.25);
  else cairo_set_source_rgba(cr, 0.2, 0., 0.2, 0.25);
  cairo_fill_preserve(cr);
  if (initializing) cairo_set_source_rgba(cr, 0.85, 0.85, 0., 0.75);
  else cairo_set_source_rgba(cr, 0.2, 0., 0.2, 0.75);
  cairo_stroke(cr);
  cairo_move_to(cr, x, 0.5 * h + y);
  cairo_line_to(cr, x + w, 0.5 * h + y);
  cairo_move_to(cr, 0.5 * w + x, y);
  cairo_line_to(cr, 0.5 * w + x, h + y);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void render_selection_box(cairo_t *cr, dingle_dots_t *dd) {
  cairo_save(cr);
  cairo_rectangle(cr, dd->selection_rect.x, dd->selection_rect.y,
	 dd->selection_rect.width, dd->selection_rect.height);
	cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.375);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.75, 0.75, 0.75, 0.875);
	cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static int cmp_z_order(const void *p1, const void *p2) {
  int ret;
  if (((const sound_shape *)p1)->z > ((const sound_shape *)p2)->z) ret = 1;
  if (((const sound_shape *)p1)->z == ((const sound_shape *)p2)->z) ret = 0;
  if (((const sound_shape *)p1)->z < ((const sound_shape *)p2)->z) ret = -1;
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

static void process_image(cairo_t *cr, const void *p, const int size, int do_tld,
 void *arg) {
  dingle_dots_t *dd = (dingle_dots_t *)arg;
  static int first_call = 1;
  static int first_data = 1;
  struct timespec ts;
	static uint32_t save_buf[2*8192*8192];
  static uint32_t save_buf2[2*8192*8192];
  static int save_size = 0;
  static ccv_comp_t newbox;
  static int made_first_tld = 0;
  int nij,s, i,j;
	float sum, bw, bw2, diff, fr, fg, fb;
	int istart, jstart, iend, jend;
	int npts;
  unsigned char y0, y1, u, v;
  unsigned char r, g, b;
	uint32_t val;
  int n;
  unsigned char *ptr = (unsigned char *) p;
	if (video_done) return;
  if (size != 0) {
    save_size = size;
  	if (!first_data) {
			memcpy(save_buf2, save_buf, 4 * dd->camera_rect.width *
			 dd->camera_rect.height);
		}
		for (n = 0; n < save_size; n += 4) {
      nij = (int) n / 2;
      i = nij%dd->camera_rect.width;
      j = nij/dd->camera_rect.width;
      y0 = (unsigned char)ptr[n + 0];
      u = (unsigned char)ptr[n + 1];
      y1 = (unsigned char)ptr[n + 2];
      v = (unsigned char)ptr[n + 3];
      YUV2RGB(y0, u, v, &r, &g, &b);
      save_buf[dd->camera_rect.width - 1 - i + j*dd->camera_rect.width] = 255 << 24 | r << 16 | g << 8 | b;
      YUV2RGB(y1, u, v, &r, &g, &b);
      save_buf[dd->camera_rect.width - 1 - (i+1) + j*dd->camera_rect.width] = 255 << 24 | r << 16 | g << 8 | b;
    }
  }
	if (first_data) {
		first_data = 0;
		memcpy(save_buf2, save_buf, 4 * dd->camera_rect.width *
		 dd->camera_rect.height);
	}
  memcpy(dd->drawing_frame->data[0], save_buf, 4 * dd->camera_rect.width *
	 dd->camera_rect.height);
  cairo_surface_t *csurf;
  csurf = cairo_image_surface_create_for_data((unsigned char *)dd->drawing_frame->data[0],
   CAIRO_FORMAT_ARGB32, dd->camera_rect.width, dd->camera_rect.height,
	 4*dd->camera_rect.width);
  cairo_t *tcr;
  tcr = cairo_create(csurf);
	if (dd->doing_motion) {
		for (s = 0; s < MAX_NSOUND_SHAPES; s++) {
			if (!dd->sound_shapes[s].active) continue;
			sum = 0;
			npts = 0;
      istart = min(dd->camera_rect.width, max(0, round(dd->sound_shapes[s].x -
			 dd->sound_shapes[s].r)));
      jstart = min(dd->camera_rect.height, max(0, round(dd->sound_shapes[s].y -
			 dd->sound_shapes[s].r)));
      iend = max(0, min(dd->camera_rect.width, round(dd->sound_shapes[s].x +
		   dd->sound_shapes[s].r)));
      jend = max(0, min(dd->camera_rect.height, round(dd->sound_shapes[s].y +
			 dd->sound_shapes[s].r)));
			for (i = istart; i < iend; i++) {
				for (j = jstart; j < jend; j++) {
					if (sound_shape_in(&dd->sound_shapes[s], i, j)) {
						val = save_buf[i + j * dd->camera_rect.width];
						fr = ((val & 0x00ff0000) >> 16) / 256.;
						fg = ((val & 0x0000ff00) >> 8) / 256.;
						fb = ((val & 0x000000ff)) / 256.;
						bw = fr * 0.3 + fg * 0.59 + fb * 0.1;
						val = save_buf2[i + j * dd->camera_rect.width];
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
				dd->sound_shapes[s].motion_state = 1;
			} else {
				dd->sound_shapes[s].motion_state = 0;
			}
		}
	} else {
		for (s = 0; s < MAX_NSOUND_SHAPES; s++) {
			dd->sound_shapes[s].motion_state = 0;
		}
	}
	if (dd->doing_tld) {
    if (do_tld) {
      sws_scale(dd->analysis_resize, (uint8_t const * const *)dd->drawing_frame->data,
       dd->drawing_frame->linesize, 0, dd->drawing_frame->height, dd->analysis_frame->data, dd->analysis_frame->linesize);
      if (make_new_tld == 1) {
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
        make_new_tld = 0;
      } else {
        ccv_read(dd->analysis_frame->data[0], &cdm2, CCV_IO_ABGR_RAW |
         CCV_IO_GRAY, dd->analysis_rect.height, dd->analysis_rect.width, 4*dd->analysis_rect.width);
        ccv_tld_info_t info;
        newbox = ccv_tld_track_object(tld, cdm, cdm2, &info);
        cdm = cdm2;
        cdm2 = 0;
      }
    }
    if (newbox.rect.width && newbox.rect.height &&
     made_first_tld && tld->found) {
      for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
        if (!dd->sound_shapes[i].active) continue;
        if (sound_shape_in(&dd->sound_shapes[i],
         dd->ascale_factor_x*newbox.rect.x +
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
      	sound_shape_on(&dd->sound_shapes[i]);
      }
    }
		if (!dd->sound_shapes[i].double_clicked_on && !dd->sound_shapes[i].motion_state
		 && !dd->sound_shapes[i].tld_state) {
			if (dd->sound_shapes[i].on) {
				sound_shape_off(&dd->sound_shapes[i]);
			}
		}
	}
	isort(dd->sound_shapes, MAX_NSOUND_SHAPES, sizeof(sound_shape), cmp_z_order);
  for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
    if (!dd->sound_shapes[i].active) continue;
    sound_shape_render(&dd->sound_shapes[i], tcr);
  }
#if defined(RENDER_KMETERS)
	for (i = 0; i < 2; i++) {
    kmeter_render(&dd->meters[i], tcr, 1.);
  }
#endif
#if defined(RENDER_FFT)
	double space;
  double w;
  double h;
  double x;
  w = dd->camera_rect.width / (FFT_SIZE + 1);
  space = w;
  cairo_save(tcr);
  cairo_set_source_rgba(tcr, 0, 0.25, 0, 0.5);
  for (i = 0; i < FFT_SIZE/2; i++) {
    h = ((20.0 * log(sqrt(fftw_out[i][0] * fftw_out[i][0] +
     fftw_out[i][1] * fftw_out[i][1])) / M_LN10) + 50.) * (dd->camera_rect.height / 50.);
    x = i * (w + space) + space;
    cairo_rectangle(tcr, x, dd->camera_rect.height - h, w, h);
    cairo_fill(tcr);
  }
  cairo_restore(tcr);
#endif
  if (dd->smdown) {
    render_detection_box(tcr, 1, dd->user_tld_rect.x, dd->user_tld_rect.y,
		 dd->user_tld_rect.width, dd->user_tld_rect.height);
  }
	if (dd->selection_in_progress) {
		render_selection_box(tcr, dd);
		for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
			if (!dd->sound_shapes[i].active) continue;
  		GdkRectangle ss_rect;
			ss_rect.x = dd->sound_shapes[i].x - dd->sound_shapes[i].r;
			ss_rect.y = dd->sound_shapes[i].y - dd->sound_shapes[i].r;
			ss_rect.width = 2 * dd->sound_shapes[i].r;
			ss_rect.height = 2 * dd->sound_shapes[i].r;
			if (gdk_rectangle_intersect(&ss_rect, &dd->selection_rect, NULL)) {
				dd->sound_shapes[i].selected = 1;
				dd->sound_shapes[i].selected_pos.x = dd->sound_shapes[i].x;
				dd->sound_shapes[i].selected_pos.y = dd->sound_shapes[i].y;
			} else {
				dd->sound_shapes[i].selected = 0;
			}
		}
	}
  render_pointer(tcr, dd->mouse_pos.x, dd->mouse_pos.y);
  if (dd->doing_tld) {
    render_detection_box(tcr, 0, dd->ascale_factor_x*newbox.rect.x,
     dd->ascale_factor_y*newbox.rect.y, dd->ascale_factor_x*newbox.rect.width,
     dd->ascale_factor_y*newbox.rect.height);
  }
  sws_scale(dd->screen_resize, (const uint8_t* const*)dd->drawing_frame->data,
	 dd->drawing_frame->linesize, 0, dd->camera_rect.height, dd->screen_frame->data,
	 dd->screen_frame->linesize);
  cairo_surface_t *screen_surf;
  screen_surf = cairo_image_surface_create_for_data((unsigned char *)dd->screen_frame->data[0],
   CAIRO_FORMAT_ARGB32, dd->screen_frame->width, dd->screen_frame->height,
	 dd->screen_frame->linesize[0]);
	cairo_set_source_surface(cr, screen_surf, 0.0, 0.0);
	cairo_paint(cr);
	cairo_destroy(tcr);
	cairo_surface_destroy(screen_surf);
	cairo_surface_destroy(csurf);
	clock_gettime(CLOCK_REALTIME, &ts);
	int drawing_size = 4 * dd->drawing_frame->width * dd->drawing_frame->height;
  int tsize = drawing_size + sizeof(struct timespec);
	if (dd->do_snapshot) {
		if (jack_ringbuffer_write_space(dd->snapshot_thread_info.ring_buf) >= tsize) {
			jack_ringbuffer_write(dd->snapshot_thread_info.ring_buf, (void *)dd->drawing_frame->data[0],
    	 drawing_size);
			jack_ringbuffer_write(dd->snapshot_thread_info.ring_buf, (void *)&ts,
    	 sizeof(ts));
    	if (pthread_mutex_trylock(&dd->snapshot_thread_info.lock) == 0) {
    	  pthread_cond_signal(&dd->snapshot_thread_info.data_ready);
    	  pthread_mutex_unlock(&dd->snapshot_thread_info.lock);
    	}
  	}
		dd->do_snapshot = 0;
  }
  if (recording_stopped) {
    if (pthread_mutex_trylock(&dd->video_thread_info.lock) == 0) {
      pthread_cond_signal(&dd->video_thread_info.data_ready);
      pthread_mutex_unlock(&dd->video_thread_info.lock);
    }
  }
  if (!recording_started || recording_stopped) return;
  if (first_call) {
    first_call = 0;
    dd->video_thread_info.stream.first_time = dd->out_frame_ts;
    can_capture = 1;
  }
  tsize = drawing_size + sizeof(struct timespec);
  if (jack_ringbuffer_write_space(video_ring_buf) >= tsize) {
    jack_ringbuffer_write(video_ring_buf, (void *)dd->drawing_frame->data[0],
     drawing_size);
    jack_ringbuffer_write(video_ring_buf, (void *)&dd->out_frame_ts,
     sizeof(struct timespec));
    if (pthread_mutex_trylock(&dd->video_thread_info.lock) == 0) {
      pthread_cond_signal(&dd->video_thread_info.data_ready);
      pthread_mutex_unlock(&dd->video_thread_info.lock);
    }
  }
}

static int read_frame(cairo_t *cr, dingle_dots_t *dd) {
  struct v4l2_buffer buf;
  int eagain = 0;
  CLEAR(buf);
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
    switch (errno) {
      case EAGAIN:
        eagain = 1;
        break;
      case EIO:
        /* Could ignore EIO, see spec. */
        /* fall through */
      default:
        errno_exit("VIDIOC_DQBUF");
    }
  }
  assert(buf.index < n_buffers);
  clock_gettime(CLOCK_MONOTONIC, &dd->out_frame_ts);
  process_image(cr, buffers[buf.index].start, buf.bytesused, !eagain, dd);
  if (!eagain) {
    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
      errno_exit("VIDIOC_QBUF");
  }
  return 1;
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

double hanning_window(int i, int N) {
  return 0.5 * (1 - cos(2 * M_PI * i / (N - 1)));
}

int process(jack_nframes_t nframes, void *arg) {
  dingle_dots_t *dd = (dingle_dots_t *)arg;
  int chn;
  size_t i;
  static int first_call = 1;
  if (can_process) {
    midi_process_output(nframes, dd);
    for (chn = 0; chn < nports; chn++) {
      in[chn] = jack_port_get_buffer(in_ports[chn], nframes);
      kmeter_process(&dd->meters[chn], in[chn], nframes);
    }
    if (nframes >= FFT_SIZE) {
      for (i = 0; i < FFT_SIZE; i++) {
        fftw_in[i][0] = in[0][i] * hanning_window(i, FFT_SIZE);
        fftw_in[i][1] = 0.0;
      }
      fftw_execute(p);
    }
  }
  if ((!can_process) || (!can_capture) || (audio_done)) return 0;
  if (first_call) {
    struct timespec *ats = &dd->audio_thread_info.stream.first_time;
    clock_gettime(CLOCK_MONOTONIC, ats);
    dd->audio_thread_info.stream.samples_count = 0;
    first_call = 0;
  }
  for (i = 0; i < nframes; i++) {
    for (chn = 0; chn < nports; chn++) {
      if (jack_ringbuffer_write (audio_ring_buf, (void *) (in[chn]+i),
       sample_size) < sample_size) {
        printf("jack overrun: %ld\n", ++jack_overruns);
      }
    }
  }
  if (pthread_mutex_trylock (&dd->audio_thread_info.lock) == 0) {
    pthread_cond_signal (&dd->audio_thread_info.data_ready);
    pthread_mutex_unlock (&dd->audio_thread_info.lock);
  }
  return 0;
}

void jack_shutdown (void *arg) {
  printf("JACK shutdown\n");
  abort();
}

void setup_jack(dingle_dots_t *dd) {
  unsigned int i;
  size_t in_size;
  can_process = 0;
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
  in_ports = (jack_port_t **) malloc(sizeof(jack_port_t *) * nports);
  out_ports = (jack_port_t **) malloc(sizeof(jack_port_t *) * nports);
  in_size =  nports * sizeof (jack_default_audio_sample_t *);
  in = (jack_default_audio_sample_t **) malloc (in_size);
  audio_ring_buf = jack_ringbuffer_create (nports * sample_size *
   16384);
  memset(in, 0, in_size);
  memset(audio_ring_buf->buf, 0, audio_ring_buf->size);
  dd->midi_ring_buf = jack_ringbuffer_create(MIDI_RB_SIZE);
  fftw_in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
  fftw_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
  p = fftw_plan_dft_1d(FFT_SIZE, fftw_in, fftw_out, FFTW_FORWARD, FFTW_ESTIMATE);
  for (i = 0; i < nports; i++) {
    char name[64];
    sprintf(name, "input%d", i + 1);
    if ((in_ports[i] = jack_port_register (dd->client, name, JACK_DEFAULT_AUDIO_TYPE,
     JackPortIsInput, 0)) == 0) {
      printf("cannot register input port \"%s\"!\n", name);
      jack_client_close(dd->client);
      exit(1);
    }
    sprintf(name, "output%d", i + 1);
    if ((out_ports[i] = jack_port_register (dd->client, name, JACK_DEFAULT_AUDIO_TYPE,
     JackPortIsOutput, 0)) == 0) {
      printf("cannot register output port \"%s\"!\n", name);
      jack_client_close(dd->client);
      exit(1);
    }
  }
  dd->midi_port = jack_port_register(dd->client, "output_midi",
   JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
  can_process = 1;
}

void teardown_jack(dingle_dots_t *dd) {
	while (jack_ringbuffer_read_space(dd->midi_ring_buf)) {
		struct timespec pause;
		pause.tv_sec = 0;
		pause.tv_nsec = 1000;
		nanosleep(&pause, NULL);
	}
	jack_client_close(dd->client);
}

void start_recording_threads(dingle_dots_t *dd) {
  recording_started = 1;
  pthread_create(&dd->audio_thread_info.thread_id, NULL, audio_disk_thread,
   dd);
  pthread_create(&dd->video_thread_info.thread_id, NULL, video_disk_thread,
   dd);
}

static gboolean configure_event_cb (GtkWidget *widget,
 GdkEventConfigure *event, gpointer data) {
  int ret;
  dingle_dots_t *dd;
  dd = (dingle_dots_t *)data;
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
	 dd->screen_frame->format, 32);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }
  if (dd->csurface)
    cairo_surface_destroy(dd->csurface);
  dd->csurface = gdk_window_create_similar_surface(gtk_widget_get_window (widget),
   CAIRO_CONTENT_COLOR, gtk_widget_get_allocated_width (widget),
   gtk_widget_get_allocated_height (widget));
	dd->screen_resize = sws_getCachedContext(dd->screen_resize, dd->camera_rect.width, dd->camera_rect.height,
	 dd->drawing_frame->format, event->width, event->height, dd->screen_frame->format,
	 SWS_FAST_BILINEAR, NULL, NULL, NULL);
  if (dd->cr) {
		cairo_destroy(dd->cr);
	}
	dd->cr = cairo_create(dd->csurface);

  /* We've handled the configure event, no need for further processing.
   * */
  return TRUE;
}

static gboolean window_state_event_cb (GtkWidget *widget,
 GdkEventWindowState *event, gpointer   data) {
  dingle_dots_t *dd;
  dd = (dingle_dots_t *)data;
  if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) {
		dd->fullscreen = TRUE;
	} else {
		dd->fullscreen = FALSE;
	}
	return FALSE;
}

static gboolean draw_cb (GtkWidget *widget, cairo_t *crt, gpointer   data)
{
  dingle_dots_t *dd;
  dd = (dingle_dots_t *)data;
  cairo_set_source_surface(crt, dd->csurface, 0, 0);
  read_frame(crt, dd);
  return FALSE;
}

static gboolean on_timeout(GtkWidget *widget) {
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean motion_notify_event_cb(GtkWidget *widget,
 GdkEventMotion *event, gpointer data) {
  int i;
  dingle_dots_t *dd = (dingle_dots_t *)data;
  dd->mouse_pos.x = event->x * dd->camera_rect.width / dd->screen_frame->width;
  dd->mouse_pos.y = event->y * dd->camera_rect.height / dd->screen_frame->height;
	for (i = 0; i < MAX_NSOUND_SHAPES;i++) {
    if (!dd->sound_shapes[i].active) continue;
		dd->sound_shapes[i].hovered = 0;
	}
	for (i = MAX_NSOUND_SHAPES; i >= 0; i--) {
    if (!dd->sound_shapes[i].active) continue;
		if (sound_shape_in(&dd->sound_shapes[i], dd->mouse_pos.x, dd->mouse_pos.y)) {
			dd->sound_shapes[i].hovered = 1;
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
  isort(dd->sound_shapes, MAX_NSOUND_SHAPES, sizeof(sound_shape), cmp_z_order);
  for (i = MAX_NSOUND_SHAPES-1; i > -1; i--) {
    if (!dd->sound_shapes[i].active) continue;
    if (dd->sound_shapes[i].mdown) {
			if (dd->sound_shapes[i].selected) {
				for (int j = 0; j < MAX_NSOUND_SHAPES; j++) {
					if (!dd->sound_shapes[j].active) continue;
					if (dd->sound_shapes[j].selected) {
						dd->sound_shapes[j].x = dd->mouse_pos.x -
						 dd->sound_shapes[i].mdown_pos.x +
						 dd->sound_shapes[j].selected_pos.x;
						dd->sound_shapes[j].y = dd->mouse_pos.y -
						 dd->sound_shapes[i].mdown_pos.y +
						 dd->sound_shapes[j].selected_pos.y;
					}
				}
				return TRUE;
			} else {
				dd->sound_shapes[i].x = dd->mouse_pos.x - dd->sound_shapes[i].mdown_pos.x
      	 + dd->sound_shapes[i].down_pos.x;
      	dd->sound_shapes[i].y = dd->mouse_pos.y - dd->sound_shapes[i].mdown_pos.y
      	 + dd->sound_shapes[i].down_pos.y;
  			return TRUE;
			}
    }
  }
	return FALSE;
}

static gboolean double_press_event_cb(GtkWidget *widget,
 GdkEventButton *event, gpointer data) {
  dingle_dots_t * dd = (dingle_dots_t *)data;
  if (event->type == GDK_2BUTTON_PRESS &&
	 event->button == GDK_BUTTON_PRIMARY) {
		uint8_t found = 0;
		isort(dd->sound_shapes, MAX_NSOUND_SHAPES, sizeof(sound_shape), cmp_z_order);
  	for (int i = MAX_NSOUND_SHAPES-1; i > -1; i--) {
    	if (!dd->sound_shapes[i].active) continue;
    	double x, y;
			x = event->x * dd->camera_rect.width / dd->screen_frame->width;
			y = event->y * dd->camera_rect.height / dd->screen_frame->height;
			if (sound_shape_in(&dd->sound_shapes[i], x, y)) {
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
  dingle_dots_t * dd = (dingle_dots_t *)data;
  dd->mouse_pos.x = event->x * dd->camera_rect.width / dd->screen_frame->width;
  dd->mouse_pos.y = event->y * dd->camera_rect.height / dd->screen_frame->height;
	if (event->button == GDK_BUTTON_PRIMARY) {
    dd->mdown = 1;
    dd->mdown_pos.x = dd->mouse_pos.x;
    dd->mdown_pos.y = dd->mouse_pos.y;
    isort(dd->sound_shapes, MAX_NSOUND_SHAPES,
     sizeof(sound_shape), cmp_z_order);
    for (i = MAX_NSOUND_SHAPES-1; i > -1; i--) {
      if (!dd->sound_shapes[i].active) continue;
      if (sound_shape_in(&dd->sound_shapes[i], dd->mouse_pos.x, dd->mouse_pos.y)) {
        if (!dd->sound_shapes[i].selected) {
					for (int j = 0; j < MAX_NSOUND_SHAPES; j++) {
						if (!dd->sound_shapes[j].active) continue;
						dd->sound_shapes[j].selected = 0;
					}
					dd->sound_shapes[i].selected = 1;
				}
				dd->sound_shapes[i].mdown = 1;
        dd->sound_shapes[i].mdown_pos.x = dd->mouse_pos.x;
        dd->sound_shapes[i].mdown_pos.y = dd->mouse_pos.y;
        dd->sound_shapes[i].down_pos.x = dd->sound_shapes[i].x;
        dd->sound_shapes[i].down_pos.y = dd->sound_shapes[i].y;
     		if (dd->sound_shapes[i].selected) {
					for (int j = 0; j < MAX_NSOUND_SHAPES; j++) {
						if (!dd->sound_shapes[j].active) continue;
						if (dd->sound_shapes[j].selected) {
							dd->sound_shapes[j].z = dd->next_z++;
							dd->sound_shapes[j].selected_pos.x = dd->sound_shapes[j].x;
							dd->sound_shapes[j].selected_pos.y = dd->sound_shapes[j].y;
						}
					}
				}
        dd->sound_shapes[i].z = dd->next_z++;
				return FALSE;
      }
    }
		dd->selection_in_progress = 1;
		dd->selection_rect.x = dd->mouse_pos.x;
		dd->selection_rect.y = dd->mouse_pos.y;
		dd->selection_rect.width = 0;
		dd->selection_rect.height = 0;
		return FALSE;
  }
  if (!shift_pressed && event->button == GDK_BUTTON_SECONDARY) {
    dd->smdown = 1;
    dd->mdown_pos.x = dd->mouse_pos.x;
    dd->mdown_pos.y = dd->mouse_pos.y;
    dd->user_tld_rect.x = dd->mdown_pos.x;
    dd->user_tld_rect.y = dd->mdown_pos.y;
    dd->user_tld_rect.width = 20 * dd->ascale_factor_x;
    dd->user_tld_rect.height = 20 * dd->ascale_factor_y;
		return TRUE;
  }
  if (dd->doing_tld && shift_pressed) {
    if (event->button == GDK_BUTTON_SECONDARY) {
      dd->doing_tld = 0;
			return TRUE;
    }
  }
  return FALSE;
}

static gboolean button_release_event_cb(GtkWidget *widget,
 GdkEventButton *event, gpointer data) {
  dingle_dots_t * dd;
  int i;
  dd = (dingle_dots_t *)data;
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
		dd->mdown = 0;
		dd->dragging = 0;
		dd->selection_in_progress = 0;
  	return TRUE;
  } else if (!shift_pressed && event->button == GDK_BUTTON_SECONDARY) {
    dd->smdown = 0;
    make_new_tld = 1;
    dd->doing_tld = 1;
  	return TRUE;
  }
	return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
 gpointer data) {
  dingle_dots_t *dd = (dingle_dots_t *)data;
  static int first_r = 1;
  if (event->keyval == GDK_KEY_q && (first_r != -1)) {
  	g_application_quit(dd->app);
	} else if (event->keyval == GDK_KEY_r && (first_r == 1)) {
    first_r = -1;
    start_recording_threads(dd);
  	return TRUE;
  } else if (event->keyval == GDK_KEY_r && (first_r == -1)) {
    first_r = 0;
    recording_stopped = 1;
  	return TRUE;
  } else if (event->keyval == GDK_KEY_Shift_L ||
   event->keyval == GDK_KEY_Shift_R) {
    shift_pressed = 1;
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
  if (event->keyval == GDK_KEY_Shift_L ||
   event->keyval == GDK_KEY_Shift_R) {
    shift_pressed = 0;
  	return TRUE;
  }
	return FALSE;
}

static gboolean quit_cb(GtkWidget *widget, gpointer data) {
  dingle_dots_t * dd;
  dd = (dingle_dots_t *)data;
  g_application_quit(dd->app);
  return TRUE;
}

static gboolean motion_cb(GtkWidget *widget, gpointer data) {
  dingle_dots_t * dd;
  dd = (dingle_dots_t *)data;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		dd->doing_motion = 1;
	} else {
		dd->doing_motion = 0;
	}
	return TRUE;
}

static gboolean snapshot_cb(GtkWidget *widget, gpointer data) {
  dingle_dots_t * dd;
  dd = (dingle_dots_t *)data;
	dd->do_snapshot = 1;
	return TRUE;
}

static gboolean make_scale_cb(GtkWidget *widget, gpointer data) {
  dingle_dots_t * dd;
  dd = (dingle_dots_t *)data;
	char *text_scale;
	char text_note[4];
	text_scale =
	 gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dd->scale_combo));
	sprintf(text_note, "%s",
	 gtk_combo_box_get_active_id(GTK_COMBO_BOX(dd->note_combo)));
	midi_key_t key;
	midi_key_init_by_scale_id(&key, atoi(text_note),
	 midi_scale_text_to_id(text_scale));
	dingle_dots_add_scale(dd, &key);
  return TRUE;
}

static void activate(GtkApplication *app, gpointer user_data) {
  GtkWidget *window;
  GtkWidget *ctl_window;
	GtkWidget *drawing_area;
	GtkWidget *note_hbox;
  GtkWidget *vbox;
  GtkWidget *rbutton;
  GtkWidget *qbutton;
  GtkWidget *mbutton;
	GtkWidget *snapshot_button;
  GtkWidget *make_scale_button;
	GtkWidget *aspect;
	dingle_dots_t *dd;
 	int ret;
	dd = (dingle_dots_t *)user_data;
  ccv_enable_default_cache();
  dd->user_tld_rect.width = dd->camera_rect.width/5.;
  dd->user_tld_rect.height = dd->camera_rect.height/5.;
  dd->user_tld_rect.x = dd->camera_rect.width/2.0;
  dd->user_tld_rect.y = dd->camera_rect.height/2.0 - 0.5 * dd->user_tld_rect.height;
  dd->drawing_frame = av_frame_alloc();
  dd->drawing_frame->format = AV_PIX_FMT_ARGB;
  dd->drawing_frame->width = dd->camera_rect.width;
  dd->drawing_frame->height = dd->camera_rect.height;
  ret = av_image_alloc(dd->drawing_frame->data, dd->drawing_frame->linesize,
   dd->drawing_frame->width, dd->drawing_frame->height, dd->drawing_frame->format, 1);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }
  init_output(dd);
  uint32_t rb_size = 200 * 4 * 640 * 360;
  video_ring_buf = jack_ringbuffer_create(rb_size);
  memset(video_ring_buf->buf, 0, video_ring_buf->size);
	dd->snapshot_thread_info.ring_buf = jack_ringbuffer_create(rb_size);
	memset(dd->snapshot_thread_info.ring_buf->buf, 0,
	 dd->snapshot_thread_info.ring_buf->size);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
  gtk_window_set_deletable(GTK_WINDOW (window), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(window), dd->camera_rect.width, dd->camera_rect.height);
	ctl_window = gtk_application_window_new(app);
	gtk_window_set_keep_above(GTK_WINDOW(ctl_window), TRUE);
  gtk_window_set_title(GTK_WINDOW (ctl_window), "Controls");
  g_signal_connect (window, "destroy", G_CALLBACK (quit_cb), dd);
  g_signal_connect (ctl_window, "destroy", G_CALLBACK (quit_cb), dd);
  aspect = gtk_aspect_frame_new(NULL, 0.5, 0.5, ((float)dd->camera_rect.width)/dd->camera_rect.height, FALSE);
  gtk_frame_set_shadow_type(GTK_FRAME(aspect), GTK_SHADOW_NONE);
	drawing_area = gtk_drawing_area_new();
	GdkGeometry size_hints;
	size_hints.min_aspect = ((double)dd->camera_rect.width)/dd->camera_rect.height;
	size_hints.max_aspect = ((double)dd->camera_rect.width)/dd->camera_rect.height;
  gtk_window_set_geometry_hints(GTK_WINDOW(window), NULL, &size_hints,
	 GDK_HINT_ASPECT);
	gtk_container_add(GTK_CONTAINER(aspect), drawing_area);
  note_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  rbutton = gtk_toggle_button_new_with_label("RECORD");
  qbutton = gtk_button_new_with_label("QUIT");
  mbutton = gtk_check_button_new_with_label("MOTION DETECTION");
  snapshot_button = gtk_button_new_with_label("TAKE SNAPSHOT");
	gtk_box_pack_start(GTK_BOX(vbox), rbutton, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), snapshot_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), mbutton, FALSE, FALSE, 0);
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
  gtk_box_pack_start(GTK_BOX(vbox), note_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note_hbox), dd->scale_combo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note_hbox), dd->note_combo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(note_hbox), make_scale_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), qbutton, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER (window), aspect);
  gtk_container_add (GTK_CONTAINER (ctl_window), vbox);

  /* Signals used to handle the backing surface */
  g_timeout_add(16, (GSourceFunc)on_timeout, (gpointer)drawing_area);
  g_signal_connect(qbutton, "clicked", G_CALLBACK(quit_cb), dd);
  g_signal_connect(snapshot_button, "clicked", G_CALLBACK(snapshot_cb), dd);
  g_signal_connect(make_scale_button, "clicked", G_CALLBACK(make_scale_cb), dd);
  g_signal_connect(mbutton, "toggled", G_CALLBACK(motion_cb), dd);
  g_signal_connect (drawing_area, "draw",
   G_CALLBACK (draw_cb), dd);
  g_signal_connect (drawing_area,"configure-event",
   G_CALLBACK (configure_event_cb), dd);
  g_signal_connect (window,"window-state-event",
   G_CALLBACK (window_state_event_cb), dd);
  g_signal_connect (drawing_area, "motion-notify-event",
   G_CALLBACK (motion_notify_event_cb), dd);
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
   | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
  gtk_widget_show_all (window);
  gtk_widget_show_all (ctl_window);
}

static void mainloop(dingle_dots_t *dd) {
  dd->app = G_APPLICATION(gtk_application_new("org.dsheeler.v4l2_wayland",
   G_APPLICATION_NON_UNIQUE));
  g_signal_connect(dd->app, "activate", G_CALLBACK (activate), dd);
  g_application_run(G_APPLICATION(dd->app), 0, NULL);
}

static void stop_capturing(void)
{
  enum v4l2_buf_type type;
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
    errno_exit("VIDIOC_STREAMOFF");
}

static void start_capturing(void)
{
  unsigned int i;
  enum v4l2_buf_type type;
  for (i = 0; i < n_buffers; ++i) {
    struct v4l2_buffer buf;
    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
      errno_exit("VIDIOC_QBUF");
  }
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
    errno_exit("VIDIOC_STREAMON");
}

static void uninit_device(void)
{
  unsigned int i;
  for (i = 0; i < n_buffers; ++i)
    if (-1 == munmap(buffers[i].start, buffers[i].length))
      errno_exit("munmap");
  free(buffers);
}

static void init_mmap(void)
{
  struct v4l2_requestbuffers req;
  CLEAR(req);
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s does not support "
          "memory mapping\n", dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_REQBUFS");
    }
  }
  if (req.count < 2) {
    fprintf(stderr, "Insufficient buffer memory on %s\n",
        dev_name);
    exit(EXIT_FAILURE);
  }
  buffers = calloc(req.count, sizeof(*buffers));
  if (!buffers) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
  for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
    struct v4l2_buffer buf;
    CLEAR(buf);
    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory      = V4L2_MEMORY_MMAP;
    buf.index       = n_buffers;
    if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
      errno_exit("VIDIOC_QUERYBUF");
    buffers[n_buffers].length = buf.length;
    buffers[n_buffers].start =
      mmap(NULL /* start anywhere */,
          buf.length,
          PROT_READ | PROT_WRITE /* required */,
          MAP_SHARED /* recommended */,
          fd, buf.m.offset);
    if (MAP_FAILED == buffers[n_buffers].start)
      errno_exit("mmap");
  }
}


static void init_device(int width, int height)
{
  struct v4l2_capability cap;
  struct v4l2_cropcap cropcap;
  struct v4l2_crop crop;
  struct v4l2_format fmt;
  if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s is no V4L2 device\n",
          dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_QUERYCAP");
    }
  }
  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf(stderr, "%s is no video capture device\n",
        dev_name);
    exit(EXIT_FAILURE);
  }
  if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
    fprintf(stderr, "%s does not support streaming i/o\n",
        dev_name);
    exit(EXIT_FAILURE);
  }
  /* Select video input, video standard and tune here. */
  CLEAR(cropcap);
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect; /* reset to default */
    if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
      switch (errno) {
        case EINVAL:
          /* Cropping not supported. */
          break;
        default:
          /* Errors ignored. */
          break;
      }
    }
  } else {
    /* Errors ignored. */
  }
  CLEAR(fmt);
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width       = width;
  fmt.fmt.pix.height      = height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  fmt.fmt.pix.field       = V4L2_FIELD_ANY;
  if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
    errno_exit("VIDIOC_S_FMT");
  /* Note VIDIOC_S_FMT may change width and height. */
  /* Buggy driver paranoia. */
  /*min = fmt.fmt.pix.width * 2;
  if (fmt.fmt.pix.bytesperline < min)
    fmt.fmt.pix.bytesperline = min;
  min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
  if (fmt.fmt.pix.sizeimage < min)
    fmt.fmt.pix.sizeimage = min;*/
  init_mmap();
}

static void close_device(void)
{
  if (-1 == close(fd))
    errno_exit("close");
  fd = -1;
}

static void open_device(void)
{
  struct stat st;
  if (-1 == stat(dev_name, &st)) {
    fprintf(stderr, "Cannot identify '%s': %d, %s\n",
        dev_name, errno, strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (!S_ISCHR(st.st_mode)) {
    fprintf(stderr, "%s is no device\n", dev_name);
    exit(EXIT_FAILURE);
  }
  fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);
  if (-1 == fd) {
    fprintf(stderr, "Cannot open '%s': %d, %s\n",
        dev_name, errno, strerror(errno));
    exit(EXIT_FAILURE);
  }
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

static void usage(FILE *fp, int argc, char **argv)
{
  fprintf(fp,
      "Usage: %s [options]\n\n"
      "Options:\n"
      "-d | --device name   Video device name [%s]\n"
      "-h | --help          Print this message\n"
      "-o | --output        filename of video file output\n"
      "-b | --bitrate       bit rate of video file output\n"
      "",
      argv[0], dev_name);
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
  dingle_dots_t dingle_dots;
  out_file_name = "testington.webm";
  dev_name = "/dev/video0";
	int width = 1280;
	int height = 720;
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
        out_file_name = optarg;
        break;
      case 'b':
        stream_bitrate = atoi(optarg);
        break;
      case 'w':
        width = atoi(optarg);
        break;
      case 'g':
        height = atoi(optarg);
        break;
      case 'h':
        usage(stdout, argc, argv);
        exit(EXIT_SUCCESS);
      default:
        usage(stderr, argc, argv);
        exit(EXIT_FAILURE);
    }
  }
  dingle_dots_init(&dingle_dots, width, height);
  setup_jack(&dingle_dots);
  setup_signal_handler();
  open_device();
  init_device(width, height);
  start_capturing();
  mainloop(&dingle_dots);
  stop_capturing();
  uninit_device();
  close_device();
	dingle_dots_deactivate_sound_shapes(&dingle_dots);
	dingle_dots_free(&dingle_dots);
	teardown_jack(&dingle_dots);
	fprintf(stderr, "\n");
  return 0;
}
