#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include <pthread.h>
#include <getopt.h>
#include <math.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
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
#include <wayland-client.h>
#include <linux/videodev2.h>
#include <fftw3.h>

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

static int read_frame(cairo_t *cr, dingle_dots_t *dd);
static void isort(void *base, size_t nmemb, size_t size,
 int (*compar)(const void *, const void *));

fftw_complex                   *fftw_in, *fftw_out;
fftw_plan                      p;
output_frame                   out_frame;
OutputStream                   video_st;
AVFormatContext                *oc;
AVFrame                        *screen_frame;
AVFrame                        *tframe;
AVFrame                        *frame;
char                           *out_file_name;
uint32_t                       width = 1920;
uint32_t                       height = 1080;
uint32_t                       awidth = 260;
uint32_t                       aheight = 148;
double                         ascale_factor_x;
double                         ascale_factor_y;
uint32_t                       stream_bitrate = 800000;
static AVFrame                 *aframe;
static struct SwsContext       *resize;
static struct SwsContext       *screen_resize;
static ccv_dense_matrix_t      *cdm = 0, *cdm2 = 0;
static ccv_tld_t               *tld = 0;
int                             recording_started = 0;
int                             recording_stopped = 0;
static int                      audio_done = 0, video_done = 0,
                                trailer_written = 0;
static int                      shift_pressed = 0;

static int                mdown = 0, mdown_x, mdown_y;
static int                m_x, m_y;
uint64_t                  next_z = 0;
static int                FIRST_W, FIRST_H, FIRST_X, FIRST_Y;
static int                make_new_tld = 0;
static char              *dev_name;
static int                fd = -1;
struct buffer            *buffers;
static unsigned int       n_buffers;

volatile int              can_process;
volatile int              can_capture;
jack_client_t            *client;
jack_ringbuffer_t        *video_ring_buf, *audio_ring_buf, *midi_ring_buf;
unsigned int              nports = 2;
jack_port_t             **in_ports, **out_ports, *midi_port;
jack_default_audio_sample_t **in;
jack_nframes_t            nframes;
const size_t              sample_size = sizeof(jack_default_audio_sample_t);
pthread_mutex_t           av_thread_lock = PTHREAD_MUTEX_INITIALIZER;
long                      jack_overruns = 0;

extern uint8_t major[], minor[];
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
  pthread_mutex_lock(&dd->audio_thread_info->lock);
  while(1) {
    ret = write_audio_frame(dd, oc, dd->audio_thread_info->stream);
    if (ret == 1) {
      audio_done = 1;
      break;
    }
    if (ret == 0) continue;
    if (ret == -1) pthread_cond_wait(&dd->audio_thread_info->data_ready,
     &dd->audio_thread_info->lock);
  }
  if (audio_done && video_done && !trailer_written) {
    av_write_trailer(oc);
    trailer_written = 1;
  }
  pthread_mutex_unlock(&dd->audio_thread_info->lock);
  return 0;
}

void *video_disk_thread (void *arg) {
  int ret;
  dingle_dots_t *dd = (dingle_dots_t *)arg;
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  pthread_mutex_lock(&dd->video_thread_info->lock);
  while(1) {
    ret = write_video_frame(oc, dd->video_thread_info->stream);
    if (ret == 1) {
      video_done = 1;
      break;
    }
    if (ret == 0) continue;
    if (ret == -1) pthread_cond_wait(&dd->video_thread_info->data_ready,
     &dd->video_thread_info->lock);
  }
  if (audio_done && video_done && !trailer_written) {
    av_write_trailer(oc);
    trailer_written = 1;
  }
  pthread_mutex_unlock(&dd->video_thread_info->lock);
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

ccv_tld_t *new_tld(int x, int y, int w, int h) {
  ccv_tld_param_t p = ccv_tld_default_params;
  ccv_rect_t box = ccv_rect(x, y, w, h);
  ccv_read(aframe->data[0], &cdm, CCV_IO_ARGB_RAW | CCV_IO_GRAY, aheight, awidth, 4*awidth);
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
  static uint32_t save_buf[8192*4608];
  static uint32_t save_buf2[8192*4608];
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
    //(uint32_t *)frame->data[0];
  	if (!first_data) {
			memcpy(save_buf2, save_buf, 2 * save_size);
		}
		for (n = 0; n < save_size; n += 4) {
      nij = (int) n / 2;
      i = nij%width;
      j = nij/width;
      y0 = (unsigned char)ptr[n + 0];
      u = (unsigned char)ptr[n + 1];
      y1 = (unsigned char)ptr[n + 2];
      v = (unsigned char)ptr[n + 3];
      YUV2RGB(y0, u, v, &r, &g, &b);
      save_buf[width - 1 - i + j*width] = 255 << 24 | r << 16 | g << 8 | b;
      YUV2RGB(y1, u, v, &r, &g, &b);
      save_buf[width - 1 - (i+1) + j*width] = 255 << 24 | r << 16 | g << 8 | b;
    }
  }
	if (first_data) {
		first_data = 0;
		memcpy(save_buf2, save_buf, 2 * save_size);
	}
  memcpy(frame->data[0], save_buf, 2 * save_size);
  GdkPixbuf *pb;
  cairo_surface_t *csurf;
  csurf = cairo_image_surface_create_for_data((unsigned char *)frame->data[0],
   CAIRO_FORMAT_ARGB32, width, height, 4*width);
  cairo_t *tcr;
  tcr = cairo_create(csurf);
	int motion_state[MAX_NSOUND_SHAPES];
	memset(motion_state, 0, sizeof(int) * MAX_NSOUND_SHAPES);
	int	tld_state[MAX_NSOUND_SHAPES];
	memset(tld_state, 0, sizeof(int) * MAX_NSOUND_SHAPES);
	if (dd->doing_motion) {
		for (s = 0; s < MAX_NSOUND_SHAPES; s++) {
			if (!dd->sound_shapes[s].active) continue;
			sum = 0;
			npts = 0;
      istart = min(width, max(0, round(dd->sound_shapes[s].x -
			 dd->sound_shapes[s].r)));
      jstart = min(height, max(0, round(dd->sound_shapes[s].y -
			 dd->sound_shapes[s].r)));
      iend = max(0, min(width, round(dd->sound_shapes[s].x +
		   dd->sound_shapes[s].r)));
      jend = max(0, min(height, round(dd->sound_shapes[s].y +
			 dd->sound_shapes[s].r)));
			for (i = istart; i < iend; i++) {
				for (j = jstart; j < jend; j++) {
					if (sound_shape_in(&dd->sound_shapes[s], i, j)) {
						val = save_buf[i + j * width];
						fr = ((val & 0x00ff0000) >> 16) / 256.;
						fg = ((val & 0x0000ff00) >> 8) / 256.;
						fb = ((val & 0x000000ff)) / 256.;
						bw = fr * 0.3 + fg * 0.59 + fb * 0.1;
						val = save_buf2[i + j * width];
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
				printf("%f\n", sum / npts);
				motion_state[s] = 1;
			}
		}
	}
	if (dd->doing_tld) {
    if (do_tld) {
      sws_scale(resize, (uint8_t const * const *)frame->data,
       frame->linesize, 0, frame->height, aframe->data, aframe->linesize);
      if (make_new_tld == 1) {
        if (FIRST_W > 0 && FIRST_H > 0) {
          tld = new_tld(FIRST_X/ascale_factor_x, FIRST_Y/ascale_factor_y,
           FIRST_W/ascale_factor_x, FIRST_H/ascale_factor_y);
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
        ccv_read(aframe->data[0], &cdm2, CCV_IO_ABGR_RAW |
         CCV_IO_GRAY, aheight, awidth, 4*awidth);
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
         ascale_factor_x*newbox.rect.x +
         0.5*ascale_factor_x*newbox.rect.width,
         ascale_factor_y*newbox.rect.y +
         0.5*ascale_factor_y*newbox.rect.height)) {
         	tld_state[i] = 1;
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


	for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
  	if (!dd->sound_shapes[i].active) continue;
		if (motion_state[i] || tld_state[i]) {
			if (!dd->sound_shapes[i].on) {
      	sound_shape_on(&dd->sound_shapes[i]);
      }
    }
		if (!motion_state[i] && !tld_state[i]) {
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
  w = width / (FFT_SIZE + 1);
  space = w;
  cairo_save(tcr);
  cairo_set_source_rgba(tcr, 0, 0.25, 0, 0.5);
  for (i = 0; i < FFT_SIZE/2; i++) {
    h = ((20.0 * log(sqrt(fftw_out[i][0] * fftw_out[i][0] +
     fftw_out[i][1] * fftw_out[i][1])) / M_LN10) + 50.) * (height / 50.);
    x = i * (w + space) + space;
    cairo_rectangle(tcr, x, height - h, w, h);
    cairo_fill(tcr);
  }
  cairo_restore(tcr);
#endif
	cairo_surface_destroy(csurf);
  if (mdown) {
    render_detection_box(tcr, 1, FIRST_X, FIRST_Y, FIRST_W, FIRST_H);
  }
  render_pointer(tcr, m_x, m_y);
  if (dd->doing_tld) {
    render_detection_box(tcr, 0, ascale_factor_x*newbox.rect.x,
     ascale_factor_y*newbox.rect.y, ascale_factor_x*newbox.rect.width,
     ascale_factor_y*newbox.rect.height);
  }
  sws_scale(screen_resize, (const uint8_t* const*)frame->data, frame->linesize,
   0, height, screen_frame->data, screen_frame->linesize);
  pb = gdk_pixbuf_new_from_data(screen_frame->data[0], GDK_COLORSPACE_RGB,
   TRUE, 8, screen_frame->width, screen_frame->height, 4*screen_frame->width,
   NULL, NULL);
  gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
  cairo_paint(cr);
  if (recording_stopped) {
    if (pthread_mutex_trylock(&dd->video_thread_info->lock) == 0) {
      pthread_cond_signal(&dd->video_thread_info->data_ready);
      pthread_mutex_unlock(&dd->video_thread_info->lock);
    }
  }
  if (!recording_started || recording_stopped) return;
  if (first_call) {
    first_call = 0;
    dd->video_thread_info->stream->first_time = out_frame.ts;
    can_capture = 1;
  }
  int tsize = out_frame.size + sizeof(struct timespec);
  if (jack_ringbuffer_write_space(video_ring_buf) >= tsize) {
    jack_ringbuffer_write(video_ring_buf, (void *)frame->data[0],
     out_frame.size);
    jack_ringbuffer_write(video_ring_buf, (void *)&out_frame.ts,
     sizeof(struct timespec));
    if (pthread_mutex_trylock(&dd->video_thread_info->lock) == 0) {
      pthread_cond_signal(&dd->video_thread_info->data_ready);
      pthread_mutex_unlock(&dd->video_thread_info->lock);
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
  clock_gettime(CLOCK_MONOTONIC, &out_frame.ts);
  process_image(cr, buffers[buf.index].start, buf.bytesused, !eagain, dd);
  if (!eagain) {
    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
      errno_exit("VIDIOC_QBUF");
  }
  return 1;
}

void process_midi_output(jack_nframes_t nframes) {
  int read, t;
  unsigned char *buffer;
  void *port_buffer;
  jack_nframes_t last_frame_time;
  struct midi_message ev;
  last_frame_time = jack_last_frame_time(client);
  port_buffer = jack_port_get_buffer(midi_port, nframes);
  if (port_buffer == NULL) {
    return;
  }
  jack_midi_clear_buffer(port_buffer);
  while (jack_ringbuffer_read_space(midi_ring_buf)) {
    read = jack_ringbuffer_peek(midi_ring_buf, (char *)&ev, sizeof(ev));
    if (read != sizeof(ev)) {
      jack_ringbuffer_read_advance(midi_ring_buf, read);
      continue;
    }
    t = ev.time + nframes - last_frame_time;
    /* If computed time is too much into
     * the future, we'll need
     *       to send it later. */
    if (t >= (int)nframes)
      break;
    /* If computed time is < 0, we
     * missed a cycle because of xrun.
     * */
    if (t < 0)
      t = 0;
    jack_ringbuffer_read_advance(midi_ring_buf, sizeof(ev));
    buffer = jack_midi_event_reserve(port_buffer, t, ev.len);
    memcpy(buffer, ev.data, ev.len);
  }
}

static void signal_handler(int sig) {
  jack_client_close(client);
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
    process_midi_output(nframes);
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
    struct timespec *ats = &dd->audio_thread_info->stream->first_time;
    clock_gettime(CLOCK_MONOTONIC, ats);
    dd->audio_thread_info->stream->samples_count = 0;
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
  if (pthread_mutex_trylock (&dd->audio_thread_info->lock) == 0) {
    pthread_cond_signal (&dd->audio_thread_info->data_ready);
    pthread_mutex_unlock (&dd->audio_thread_info->lock);
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
  if ((client = jack_client_open("v4l2_wayland", JackNoStartServer, NULL)) == 0) {
    printf("jack server not running?\n");
    exit(1);
  }
  jack_set_process_callback(client, process, dd);
  jack_on_shutdown(client, jack_shutdown, NULL);
  if (jack_activate(client)) {
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
  midi_ring_buf = jack_ringbuffer_create(MIDI_RB_SIZE);
  fftw_in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
  fftw_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
  p = fftw_plan_dft_1d(FFT_SIZE, fftw_in, fftw_out, FFTW_FORWARD, FFTW_ESTIMATE);
  for (i = 0; i < nports; i++) {
    char name[64];
    sprintf(name, "input%d", i + 1);
    if ((in_ports[i] = jack_port_register (client, name, JACK_DEFAULT_AUDIO_TYPE,
     JackPortIsInput, 0)) == 0) {
      printf("cannot register input port \"%s\"!\n", name);
      jack_client_close(client);
      exit(1);
    }
    sprintf(name, "output%d", i + 1);
    if ((out_ports[i] = jack_port_register (client, name, JACK_DEFAULT_AUDIO_TYPE,
     JackPortIsOutput, 0)) == 0) {
      printf("cannot register output port \"%s\"!\n", name);
      jack_client_close(client);
      exit(1);
    }
  }
  midi_port = jack_port_register(client, "output_midi",
   JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
  can_process = 1;
}

void start_recording_threads(dingle_dots_t *dd) {
  recording_started = 1;
  printf("start_recording\n");
  pthread_create(&dd->audio_thread_info->thread_id, NULL, audio_disk_thread,
   dd);
  pthread_create(&dd->video_thread_info->thread_id, NULL, video_disk_thread,
   dd);
}

int midi_scale_init(midi_scale_t *scale, uint8_t *notes, uint8_t nb_notes) {
  scale->notes = notes;
  scale->nb_notes = nb_notes;
  return 0;
}

int midi_key_init(midi_key_t *key, uint8_t base_note, midi_scale_t *scale) {
  key->base_note = base_note;
  key->scale = scale;
  return 0;
}

int dingle_dots_init(dingle_dots_t *dd, midi_key_t *keys, uint8_t nb_keys) {
  int i;
  int j;
  int k;
  char label[NCHAR];
  int note_names_idx, note_octave_num;
  uint8_t midi_note;
  double freq;
  double x_delta;
  char *note_names[] = {"C", "C#", "D", "D#", "E",
   "F", "F#", "G", "G#", "A", "A#", "B"};
  color colors[2];
  color_init(&colors[0], 30./255., 100./255., 80./255., 0.5);
  color_init(&colors[1], 0., 30./255., 80./255., 0.5);
	dd->doing_tld = 0;
	dd->doing_motion = 0;
	dd->motion_threshold = 0.001;
	memset(dd->sound_shapes, 0, MAX_NSOUND_SHAPES * sizeof(sound_shape));
  for (i = 0; i < nb_keys; i++) {
    x_delta = 1. / (keys[i].scale->nb_notes + 1);
    for (j = 0; j < keys[i].scale->nb_notes; j++) {
      midi_note = keys[i].base_note + keys[i].scale->notes[j];
      note_octave_num = (midi_note - 12) / 12;
      note_names_idx = (midi_note - 12) % 12;
      freq = 440.0 * pow(2.0, (midi_note - 69.0) / 12.0);
      memset(label, '\0', NCHAR * sizeof(char));
      sprintf(label, "%d\n%.2f\n%s%d", j+1, freq, note_names[note_names_idx],
     note_octave_num);
      for (k = 0; k < MAX_NSOUND_SHAPES; k++) {
        if (dd->sound_shapes[k].active) continue;
        sound_shape_init(&dd->sound_shapes[k], label, midi_note,
         x_delta * (j + 1) * width, (i+1) * height / 4.,
         width/(keys[i].scale->nb_notes*3),
         &colors[i%2]);
        sound_shape_activate(&dd->sound_shapes[k]);
        break;
      }
    }
  }
  color meter_color;
  color_init(&meter_color, 30./255., 0, 0, 0.5);
  for (i = 0; i < 2; i++) {
    float w = width/3.;
    kmeter_init(&dd->meters[i], 48000, 256, 0.5, 15.0, w * (i + 1),
     height - w, w, meter_color);
  }
  return 0;
}

static void close_window (void *data) {
	dingle_dots_t *dd = (dingle_dots_t *)data;
	if(dd->csurface)
    cairo_surface_destroy(dd->csurface);
  gtk_main_quit();
}

static gboolean configure_event_cb (GtkWidget *widget,
 GdkEventConfigure *event, gpointer data) {
  int ret;
  dingle_dots_t *dd;
  dd = (dingle_dots_t *)data;
  if (dd->csurface)
    cairo_surface_destroy(dd->csurface);
  dd->csurface = gdk_window_create_similar_surface(gtk_widget_get_window (widget),
   CAIRO_CONTENT_COLOR, gtk_widget_get_allocated_width (widget),
   gtk_widget_get_allocated_height (widget));
  screen_resize = sws_getContext(width, height, AV_PIX_FMT_RGBA,
   event->width, event->height, AV_PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL,
   NULL);
  dd->cr = cairo_create(dd->csurface);
  screen_frame = av_frame_alloc();
  screen_frame->format = AV_PIX_FMT_BGRA;
  screen_frame->width = event->width;
  screen_frame->height = event->height;
  ret = av_image_alloc(screen_frame->data, screen_frame->linesize,
   screen_frame->width, screen_frame->height, screen_frame->format, 1);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }

  /* We've handled the configure event, no need for further processing.
   * */
  return TRUE;
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
  m_x = event->x * width / screen_frame->width;
  m_y = event->y * height / screen_frame->height;
  if (mdown) {
    FIRST_W = m_x - mdown_x;
    FIRST_H = m_y - mdown_y;
    if (FIRST_W < 0) {
      if (FIRST_W > - 20 * ascale_factor_x) {
        FIRST_W = 20 * ascale_factor_x;
      } else {
        FIRST_W = -FIRST_W;
      }
      FIRST_X = mdown_x - FIRST_W;
    } else if (FIRST_W >= 0) {
      if (FIRST_W < 20 * ascale_factor_x) {
        FIRST_W = 20 * ascale_factor_x;
      }
      FIRST_X = mdown_x;
    }
    if (FIRST_H < 0) {
      if (FIRST_H > - 20 * ascale_factor_y) {
        FIRST_H = 20 * ascale_factor_y;
      } else {
        FIRST_H = -FIRST_H;
      }
      FIRST_Y = mdown_y - FIRST_H;
    } else if (FIRST_W >= 0) {
      if (FIRST_H < 20 * ascale_factor_y) {
        FIRST_H = 20 * ascale_factor_y;
      }
      FIRST_Y = mdown_y;
    }
  }
  isort(dd->sound_shapes, MAX_NSOUND_SHAPES, sizeof(sound_shape), cmp_z_order);
  for (i = MAX_NSOUND_SHAPES-1; i > -1; i--) {
    if (!dd->sound_shapes[i].active) continue;
    if (dd->sound_shapes[i].mdown) {
      dd->sound_shapes[i].x = m_x - dd->sound_shapes[i].mdown_x
       + dd->sound_shapes[i].down_x;
      dd->sound_shapes[i].y = m_y - dd->sound_shapes[i].mdown_y
       + dd->sound_shapes[i].down_y;
      break;
    }
  }
  return TRUE;
}

static gboolean button_press_event_cb(GtkWidget *widget,
 GdkEventButton *event, gpointer data) {
  int i;
  dingle_dots_t * dd = (dingle_dots_t *)data;
  if (event->button == GDK_BUTTON_PRIMARY) {
    isort(dd->sound_shapes, MAX_NSOUND_SHAPES,
     sizeof(sound_shape), cmp_z_order);
    for (i = MAX_NSOUND_SHAPES-1; i > -1; i--) {
      if (!dd->sound_shapes[i].active) continue;
      if (sound_shape_in(&dd->sound_shapes[i], m_x, m_y)) {
        dd->sound_shapes[i].mdown = 1;
        dd->sound_shapes[i].mdown_x = m_x;
        dd->sound_shapes[i].mdown_y = m_y;
        dd->sound_shapes[i].down_x = dd->sound_shapes[i].x;
        dd->sound_shapes[i].down_y = dd->sound_shapes[i].y;
        dd->sound_shapes[i].z = next_z++;
        break;
      }
    }
  }
  if (!shift_pressed && event->button == GDK_BUTTON_SECONDARY) {
    mdown = 1;
    mdown_x = m_x;
    mdown_y = m_y;
    FIRST_X = mdown_x;
    FIRST_Y = mdown_y;
    FIRST_W = 20 * ascale_factor_x;
    FIRST_H = 20 * ascale_factor_y;
  }
  if (dd->doing_tld && shift_pressed) {
    if (event->button == GDK_BUTTON_SECONDARY) {
      dd->doing_tld = 0;
    }
  }
  return TRUE;
}

static gboolean button_release_event_cb(GtkWidget *widget,
 GdkEventButton *event, gpointer data) {
  dingle_dots_t * dd;
  int i;
  dd = (dingle_dots_t *)data;
  if (event->button == GDK_BUTTON_PRIMARY) {
    for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
      if (!dd->sound_shapes[i].active) continue;
      dd->sound_shapes[i].mdown = 0;
    }
  } else if (!shift_pressed && event->button == GDK_BUTTON_SECONDARY) {
    mdown = 0;
    make_new_tld = 1;
    dd->doing_tld = 1;
  }
  return TRUE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
 gpointer data) {
  dingle_dots_t *dd = (dingle_dots_t *)data;
  static int first_r = 1;
  if (event->keyval == GDK_KEY_q && (first_r != -1)) {
    exit(0);
  } else if (event->keyval == GDK_KEY_r && (first_r == 1)) {
    first_r = -1;
    start_recording_threads(dd);
  } else if (event->keyval == GDK_KEY_r && (first_r == -1)) {
    first_r = 0;
    recording_stopped = 1;
  } else if (event->keyval == GDK_KEY_Shift_L ||
   event->keyval == GDK_KEY_Shift_R) {
    shift_pressed = 1;
  }
  return TRUE;
}

static gboolean on_key_release(GtkWidget *widget, GdkEventKey *event,
 gpointer data) {
  if (event->keyval == GDK_KEY_Shift_L ||
   event->keyval == GDK_KEY_Shift_R) {
    shift_pressed = 0;
  }
  return TRUE;
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

static void activate(GtkApplication *app, gpointer user_data) {
  GtkWidget *window;
  GtkWidget *gframe;
  GtkWidget *drawing_area;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkWidget *rbutton;
  GtkWidget *qbutton;
  GtkWidget *mbutton;
  dingle_dots_t *dd;
  int ret;
	dd = (dingle_dots_t *)user_data;
  ccv_enable_default_cache();
  FIRST_W = width/5.;
  FIRST_H = height/5.;
  FIRST_X = width/2.0;
  FIRST_Y = height/2.0 - 0.5 * FIRST_H;
  printf("width, awidth: %d, %d, %f\n", width, awidth, (1.0*width)/(1.0*awidth));
  ascale_factor_x = (1.0*width) / awidth;
  ascale_factor_y = ((double)height) / aheight;
  resize = sws_getContext(width, height, AV_PIX_FMT_ARGB, awidth,
   aheight, AV_PIX_FMT_ARGB, SWS_BICUBIC, NULL, NULL, NULL);
  frame = av_frame_alloc();
  frame->format = AV_PIX_FMT_ARGB;
  frame->width = width;
  frame->height = height;
  ret = av_image_alloc(frame->data, frame->linesize,
   frame->width, frame->height, frame->format, 1);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }
	tframe = av_frame_alloc();
  tframe->format = AV_PIX_FMT_BGRA;
  tframe->width = width;
  tframe->height = height;
  ret = av_image_alloc(tframe->data, tframe->linesize,
   tframe->width, tframe->height, tframe->format, 1);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }
  aframe = av_frame_alloc();
  aframe->format = AV_PIX_FMT_ARGB;
  aframe->width = awidth;
  aframe->height = aheight;
  ret = av_image_alloc(aframe->data, aframe->linesize,
   aframe->width, aframe->height, aframe->format, 1);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }
  midi_key_t keys[2];
  midi_scale_t scales[2];
  midi_scale_init(&scales[0], minor, 8);
  midi_scale_init(&scales[1], major, 8);
  midi_key_init(&keys[0], 32, &scales[0]);
  midi_key_init(&keys[1], 44, &scales[1]);
  dingle_dots_init(dd, keys, 2);
  init_output(dd);
  out_frame.size = 4 * width * height;
  out_frame.data = calloc(1, out_frame.size);
  uint32_t rb_size = 200 * 4 * 640 * 360 / out_frame.size;
  video_ring_buf = jack_ringbuffer_create(rb_size*out_frame.size);
  memset(video_ring_buf->buf, 0, video_ring_buf->size);  window = gtk_application_window_new (app);
  gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
  //gtk_window_set_title (GTK_WINDOW (window), "Drawing Area");
  g_signal_connect (window, "destroy", G_CALLBACK (close_window), dd);
  gtk_container_set_border_width (GTK_CONTAINER (window), 64);
  gframe = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (gframe), GTK_SHADOW_OUT);
  gtk_container_add (GTK_CONTAINER (window), gframe);
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  rbutton = gtk_toggle_button_new_with_label("RECORD");
  qbutton = gtk_button_new_with_label("QUIT");
  mbutton = gtk_toggle_button_new_with_label("MOTION DETECTION");
  gtk_box_pack_start(GTK_BOX(vbox), rbutton, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), qbutton, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), mbutton, FALSE, FALSE, 0);
  drawing_area = gtk_drawing_area_new ();
  gtk_widget_set_size_request (drawing_area, width, height);
  gtk_box_pack_start(GTK_BOX(hbox), drawing_area, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
  gtk_container_add (GTK_CONTAINER (gframe), hbox);
  /* Signals used to handle the backing surface */
  g_timeout_add(16, (GSourceFunc)on_timeout, (gpointer)drawing_area);
  g_signal_connect(qbutton, "clicked", G_CALLBACK(quit_cb), dd);
  g_signal_connect(mbutton, "toggled", G_CALLBACK(motion_cb), dd);
  g_signal_connect (drawing_area, "draw",
   G_CALLBACK (draw_cb), dd);
  g_signal_connect (drawing_area,"configure-event",
   G_CALLBACK (configure_event_cb), dd);
  g_signal_connect (drawing_area, "motion-notify-event",
   G_CALLBACK (motion_notify_event_cb), dd);
  g_signal_connect (drawing_area, "button-press-event",
   G_CALLBACK (button_press_event_cb), dd);
  g_signal_connect (drawing_area, "button-release-event",
   G_CALLBACK (button_release_event_cb), dd);
  g_signal_connect (window, "key-press-event",
   G_CALLBACK (on_key_press), dd);
  g_signal_connect (window, "key-release-event",
   G_CALLBACK (on_key_release), dd);
  gtk_widget_set_events (drawing_area, gtk_widget_get_events (drawing_area)
   | GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK
   | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
  gtk_widget_show_all (window);
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


static void init_device(void)
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
  fmt.fmt.pix.width       = width; //replace
  fmt.fmt.pix.height      = height; //replace
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; //replace
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
  disk_thread_info_t video_thread_info;
  disk_thread_info_t audio_thread_info;
  OutputStream  audio_st;
  dingle_dots_t dingle_dots;
  dingle_dots.audio_thread_info = &audio_thread_info;
  dingle_dots.video_thread_info = &video_thread_info;
  dingle_dots.audio_thread_info->stream = &audio_st;
  dingle_dots.video_thread_info->stream = &video_st;
  pthread_mutex_init(&video_thread_info.lock, NULL);
  pthread_mutex_init(&audio_thread_info.lock, NULL);
  pthread_cond_init(&video_thread_info.data_ready, NULL);
  pthread_cond_init(&audio_thread_info.data_ready, NULL);
  out_file_name = "testington.webm";
  dev_name = "/dev/video0";
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
        ascale_factor_x = ((double)width) / awidth;
        break;
      case 'g':
        height = atoi(optarg);
        ascale_factor_y = ((double)height) / aheight;
        break;
      case 'x':
        awidth = atoi(optarg);
        ascale_factor_x = ((double)width) / awidth;
        break;
      case 'y':
        aheight = atoi(optarg);
        ascale_factor_y = ((double)height) / aheight;
        break;
      case 'h':
        usage(stdout, argc, argv);
        exit(EXIT_SUCCESS);
      default:
        usage(stderr, argc, argv);
        exit(EXIT_FAILURE);
    }
  }
  setup_jack(&dingle_dots);
  setup_signal_handler();
  open_device();
  init_device();
  start_capturing();
  mainloop(&dingle_dots);
  stop_capturing();
  uninit_device();
  close_device();
  fprintf(stderr, "\n");
  return 0;
}
