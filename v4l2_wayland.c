/*
 *  V4L2 video capture example
 *
 *  This program can be used and distributed without restrictions.
 *
 *      This program is provided with the V4L2 API
 * see http://linuxtv.org/docs.php for more information
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include <pthread.h>
#include <getopt.h>             /* getopt_long() */
#include <math.h>

#include <fcntl.h>              /* low-level i/o */
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

#define MIDI_RB_SIZE 1024 * sizeof(struct midi_message)

static int read_frame(dingle_dots_t *dd);
static void isort(void *base, size_t nmemb, size_t size,
 int (*compar)(const void *, const void *));

output_frame                   out_frame;
OutputStream                   video_st;
AVFormatContext                *oc;
AVFrame                        *frame;
char                           *out_file_name;
uint32_t                       width = 640;
uint32_t                       height = 360;
uint32_t                       awidth = 260;
uint32_t                       aheight = 148;
double                         ascale_factor_x;
double                         ascale_factor_y;
uint32_t                       stream_bitrate = 400000;
static AVFrame                 *aframe;
static struct SwsContext       *resize;
static ccv_dense_matrix_t      *cdm = 0, *cdm2 = 0;
static ccv_tld_t               *tld = 0;
static struct wl_compositor    *compositor;
static struct wl_display       *display;
static struct wl_keyboard      *keyboard;
static struct wl_pointer       *pointer;
static struct wl_seat          *seat;
static struct wl_shell         *shell;
static struct wl_shell_surface *shell_surface;
static struct wl_shm           *shm;
static struct wl_surface       *surface;
static struct wl_buffer        *buffer;
static struct wl_callback      *frame_callback;
int                             recording_started = 0;
int                             recording_stopped = 0;
static int                      audio_done = 0, video_done = 0,
                                trailer_written = 0;
static int                      shift_pressed = 0;
void *shm_data;

static const struct wl_registry_listener registry_listener;
static const struct wl_keyboard_listener keyboard_listener;
static const struct wl_pointer_listener pointer_listener;

static cairo_surface_t    *csurface;
static cairo_t            *cr;
static int                mdown = 0, mdown_x, mdown_y;
static int                m_x, m_y;
uint64_t                  next_z = 0;
static int                FIRST_W, FIRST_H, FIRST_X, FIRST_Y;
static int                doing_tld = 0;
static int                make_new_tld = 0;
static struct pollfd      fds[1];
static char              *dev_name;
static int                fd = -1;
struct buffer            *buffers;
static unsigned int       n_buffers;
static int                frame_count = 200;
static int                frame_number = 0;

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

void setup_wayland(dingle_dots_t *dd) {
  struct wl_registry *registry;
  display = wl_display_connect(NULL);
  if (display == NULL) {
    fprintf(stderr, "Can't connect to display\n");
    exit(1);
  }
  registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, dd);
  wl_display_roundtrip(display);
  wl_registry_destroy(registry);
}

void cleanup_wayland(void)
{
  wl_pointer_destroy(pointer);
  wl_seat_destroy(seat);
  wl_shell_destroy(shell);
  wl_shm_destroy(shm);
  wl_compositor_destroy(compositor);
  wl_display_disconnect(display);
}

static void registry_global(void *data,
    struct wl_registry *registry, uint32_t name,
    const char *interface, uint32_t version)
{
  dingle_dots_t *dd = (dingle_dots_t *)data;
  if (strcmp(interface, wl_compositor_interface.name) == 0)
    compositor = wl_registry_bind(registry, name,
     &wl_compositor_interface, min(version, 4));
  else if (strcmp(interface, wl_shm_interface.name) == 0) {
    shm = wl_registry_bind(registry, name,
     &wl_shm_interface, min(version, 1));
  } else if (strcmp(interface, wl_shell_interface.name) == 0)
    shell = wl_registry_bind(registry, name,
     &wl_shell_interface, min(version, 1));
  else if (strcmp(interface, wl_seat_interface.name) == 0) {
    seat = wl_registry_bind(registry, name,
     &wl_seat_interface, min(version, 2));
    pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(pointer, &pointer_listener, dd);
    keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(keyboard, &keyboard_listener, dd);
  }
}

static void registry_global_remove(void *a,
    struct wl_registry *b, uint32_t c) { }

static const struct wl_registry_listener registry_listener = {
  .global = registry_global,
  .global_remove = registry_global_remove
};

static const uint32_t PIXEL_FORMAT_ID = WL_SHM_FORMAT_ARGB8888;

static int set_cloexec_or_close(int fd)
{
  long flags;
  if (fd == -1)
    return -1;
  flags = fcntl(fd, F_GETFD);
  if (flags == -1)
    goto err;
  if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
    goto err;
  return fd;
err:
  close(fd);
  return -1;
}

static int create_tmpfile_cloexec(char *tmpname)
{
  int fd;
#ifdef HAVE_MKOSTEMP
  fd = mkostemp(tmpname, O_CLOEXEC);
  if (fd >= 0)
    unlink(tmpname);
#else
  fd = mkstemp(tmpname);
  if (fd >= 0) {
    fd = set_cloexec_or_close(fd);
    unlink(tmpname);
  }
#endif
  return fd;
}
int os_create_anonymous_file(off_t size)
{
  static const char template[] = "/weston-shared-XXXXXX";
  const char *path;
  char *name;
  int fd;
  path = getenv("XDG_RUNTIME_DIR");
  if (!path) {
    errno = ENOENT;
    return -1;
  }
  name = malloc(strlen(path) + sizeof(template));
  if (!name)
    return -1;
  strcpy(name, path);
  strcat(name, template);
  fd = create_tmpfile_cloexec(name);
  free(name);
  if (fd < 0)
    return -1;
  if (ftruncate(fd, size) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void paint_pixels() {
  int n;
  uint32_t *pixel = shm_data;
  for (n =0; n < width * height; n++) {
    *pixel++ = 0xffff00;
  }
}

static const struct wl_callback_listener frame_listener;

static void redraw(void *data, struct wl_callback *callback, uint32_t time)
{
  dingle_dots_t *dd = (dingle_dots_t *)data;
  wl_callback_destroy(frame_callback);
  wl_surface_damage(surface, 0, 0, width, height); 
  read_frame(dd);
  frame_callback = wl_surface_frame(surface);
  wl_surface_attach(surface, buffer, 0, 0);
  wl_callback_add_listener(frame_callback, &frame_listener, dd);
  wl_surface_commit(surface);
}
static const struct
wl_callback_listener frame_listener
= {
  redraw
};

struct wl_buffer *create_buffer(unsigned width, unsigned height)
{
  struct wl_shm_pool *pool;
  int stride = width * 4;
  int size = stride * height;
  int fd;
  struct wl_buffer *buffer;
  fd = os_create_anonymous_file(size);
  if (fd < 0) {
    fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
        size);
    exit(1);
  }
  shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (shm_data == MAP_FAILED) {
    fprintf(stderr, "mmap failed: %m\n");
    close(fd);
    exit(1);
  }
  pool = wl_shm_create_pool(shm, fd, size);
  buffer = wl_shm_pool_create_buffer(pool, 0,
      width, height,
      stride,
      WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  return buffer;
}

void free_buffer(struct wl_buffer *buffer)
{
  wl_buffer_destroy(buffer);
}

static void
create_window() {
  buffer = create_buffer(width, height);
  wl_surface_attach(surface, buffer, 0, 0);
  wl_surface_commit(surface);
}

static void shell_surface_ping(void *data,
    struct wl_shell_surface *shell_surface, uint32_t serial)
{
  wl_shell_surface_pong(shell_surface, serial);
}

static void shell_surface_configure(void *data,
    struct wl_shell_surface *shell_surface,
    uint32_t edges, int32_t width, int32_t height) { }

static const struct wl_shell_surface_listener
shell_surface_listener = {
  .ping = shell_surface_ping,
  .configure = shell_surface_configure,
};

struct wl_shell_surface *create_surface(void)
{
  struct wl_surface *surface;
  struct wl_shell_surface *shell_surface;
  surface = wl_compositor_create_surface(compositor);
  if (surface == NULL)
    return NULL;
  shell_surface = wl_shell_get_shell_surface(shell, surface);
  if (shell_surface == NULL) {
    wl_surface_destroy(surface);
    return NULL;
  }
  wl_shell_surface_add_listener(shell_surface,
      &shell_surface_listener, 0);
  wl_shell_surface_set_toplevel(shell_surface);
  wl_shell_surface_set_user_data(shell_surface, surface);
  wl_surface_set_user_data(surface, NULL);
  return shell_surface;
}

void free_surface(struct wl_shell_surface *shell_surface)
{
  struct wl_surface *surface;
  surface = wl_shell_surface_get_user_data(shell_surface);
  wl_shell_surface_destroy(shell_surface);
  wl_surface_destroy(surface);
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
  static int out_done = 0;
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
    printf("vdiskthread ret %d\n", ret);
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
  ccv_rect_t box = ccv_rect(x, y, w, h);
  ccv_read(aframe->data[0], &cdm, CCV_IO_ARGB_RAW | CCV_IO_GRAY, aheight, awidth, 4*awidth);
  return ccv_tld_new(cdm, box, ccv_tld_default_params);
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

static void process_image(const void *p, const int size, int do_tld,
 void *arg) {
  dingle_dots_t *dd = (dingle_dots_t *)arg;
  static int first_call = 1;
  static unsigned char save_buf[8192*4608];
  static int save_size = 0;
  static ccv_comp_t newbox;
  static int made_first_tld = 0;
  int nij, i,j;
  unsigned char y0, y1, u, v;
  unsigned char r, g, b;
  int n;
  uint32_t *pixel = shm_data;
  unsigned char *ptr = (unsigned char*) p;
  if (video_done) return;
  if (size == 0) {
    ptr = save_buf;
  } else {
    save_size = size;
    memcpy(save_buf, p, size);
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
    pixel[width - 1 - i + j*width] = r << 16 | g << 8 | b;
    YUV2RGB(y1, u, v, &r, &g, &b);
    pixel[width - 1 - (i+1) + j*width] = r << 16 | g << 8 | b;
  }
  if (doing_tld) {
    if (do_tld) {
      int ret = sws_scale(resize, (uint8_t const * const *)frame->data, frame->linesize, 0, frame->height,
          aframe->data, aframe->linesize);
      if (make_new_tld == 1) {
        if (FIRST_W > 0 && FIRST_H > 0) {
          tld = new_tld(FIRST_X/ascale_factor_x, FIRST_Y/ascale_factor_y, FIRST_W/ascale_factor_x, FIRST_H/ascale_factor_y);
        } else {
          tld = NULL;
          doing_tld = 0;
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
        ccv_read(aframe->data[0], &cdm2, CCV_IO_ARGB_RAW | CCV_IO_GRAY, aheight, awidth, 4*awidth);
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
          if (!dd->sound_shapes[i].on) {
            sound_shape_on(&dd->sound_shapes[i]);
          }
        } else {
          if (dd->sound_shapes[i].on) {
            sound_shape_off(&dd->sound_shapes[i]);
          }
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
    for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
      if (!dd->sound_shapes[i].active) continue;
      if (dd->sound_shapes[i].on) {
        sound_shape_off(&dd->sound_shapes[i]);
      }
    }
  }
  isort(dd->sound_shapes, MAX_NSOUND_SHAPES, sizeof(sound_shape), cmp_z_order);
  for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
    if (!dd->sound_shapes[i].active) continue;
    sound_shape_render(&dd->sound_shapes[i], cr);
  }
  if (mdown) {
    render_detection_box(cr, 1, FIRST_X, FIRST_Y, FIRST_W, FIRST_H);
  }
  render_pointer(cr, m_x, m_y);
  if (doing_tld) {
    render_detection_box(cr, 0, ascale_factor_x*newbox.rect.x,
     ascale_factor_y*newbox.rect.y, ascale_factor_x*newbox.rect.width,
     ascale_factor_y*newbox.rect.height);
  }
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
  int write_failed = 0;
  if (jack_ringbuffer_write_space(video_ring_buf) >= tsize) {
    jack_ringbuffer_write(video_ring_buf, (void *)shm_data,
     out_frame.size);
    jack_ringbuffer_write(video_ring_buf, (void *)&out_frame.ts,
     sizeof(struct timespec));
    if (pthread_mutex_trylock(&dd->video_thread_info->lock) == 0) {
      pthread_cond_signal(&dd->video_thread_info->data_ready);
      pthread_mutex_unlock(&dd->video_thread_info->lock);
    }
  } else {
    //dd->video_thread_info->stream->overruns++;
    //printf("VIDEO OVERRUNS: %d\n", dd->video_thread_info->stream->overruns);
  }
}

static int read_frame(dingle_dots_t *dd) {
  struct v4l2_buffer buf;
  unsigned int i;
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
  process_image(buffers[buf.index].start, buf.bytesused, !eagain, dd);
  if (!eagain) {
    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
      errno_exit("VIDIOC_QBUF");
  }
  return 1;
}

void on_button(uint32_t button) {
}

void set_button_callback(
    struct wl_shell_surface *shell_surface,
    void (*callback)(uint32_t))
{
  //struct wl_surface* surface;

  //wl_surface_set_user_data(surface, callback);
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

int process(jack_nframes_t nframes, void *arg) {
  dingle_dots_t *dd = (dingle_dots_t *)arg;
  int chn;
  size_t i;
  static int first_call = 1;
  if (can_process) process_midi_output(nframes);
  if ((!can_process) || (!can_capture) || (audio_done)) return 0;
  if (first_call) {
    struct timespec *ats = &dd->audio_thread_info->stream->first_time;
    struct timespec *vts = &dd->video_thread_info->stream->first_time;
    clock_gettime(CLOCK_MONOTONIC, ats);
    double diff;
    diff = ats->tv_sec + 1e-9*ats->tv_nsec - (vts->tv_sec +
     1e-9*vts->tv_nsec);
    dd->audio_thread_info->stream->samples_count = 0;/*
     dd->audio_thread_info->stream->enc->time_base.den * diff /
     dd->audio_thread_info->stream->enc->time_base.num;*/
    first_call = 0;
  }
  for (chn = 0; chn < nports; chn++)
    in[chn] = jack_port_get_buffer(in_ports[chn], nframes);
  for (i = 0; i < nframes; i++) {
    for (chn = 0; chn < nports; chn++) {
      if (jack_ringbuffer_write (audio_ring_buf, (void *) (in[chn]+i),
       sample_size) < sample_size) {
        printf("jack overrun: %d\n", ++jack_overruns);
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
  printf("dd_init\n");
  return 0;
}

static void mainloop(dingle_dots_t *dd) {
  int ret;
  int i;
  if (compositor == NULL) {
    fprintf(stderr, "Can't find compositor\n");
    exit(1);
  } else {
    fprintf(stderr, "Found compositor\n");
  }
  surface = wl_compositor_create_surface(compositor);
  if (surface == NULL) {
    fprintf(stderr, "Can't create surface\n");
    exit(1);
  } else {
    fprintf(stderr, "Created surface\n");
  }
  shell_surface = wl_shell_get_shell_surface(shell, surface);
  if (shell_surface == NULL) {
    fprintf(stderr, "Can't create shell surface\n");
    exit(1);
  } else {
    fprintf(stderr, "Created shell surface\n");
  }
  wl_shell_surface_set_toplevel(shell_surface);
  wl_shell_surface_add_listener(shell_surface,
      &shell_surface_listener, NULL);
  frame_callback = wl_surface_frame(surface);
  wl_callback_add_listener(frame_callback, &frame_listener, dd);
  create_window();
  ccv_enable_default_cache();
  FIRST_W = width/5.;
  FIRST_H = height/5.;
  FIRST_X = width/2.0;
  FIRST_Y = height/2.0 - 0.5 * FIRST_H;
  printf("width, awidth: %d, %d, %f\n", width, awidth, (1.0*width)/(1.0*awidth));
  ascale_factor_x = (1.0*width) / awidth;
  ascale_factor_y = ((double)height) / aheight;
  resize = sws_getContext(width, height, AV_PIX_FMT_RGB32, awidth,
   aheight, AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);
  frame = av_frame_alloc();
  frame->format = AV_PIX_FMT_RGB32;
  frame->width = width;
  frame->height = height;
  av_image_fill_arrays(frame->data, frame->linesize,
   (const unsigned char *)shm_data, AV_PIX_FMT_RGB32, frame->width, frame->height, 1);
  aframe = av_frame_alloc();
  aframe->format = AV_PIX_FMT_RGB32;
  aframe->width = awidth;
  aframe->height = aheight;
  ret = av_image_alloc(aframe->data, aframe->linesize,
   aframe->width, aframe->height, aframe->format, 1);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }
  csurface = cairo_image_surface_create_for_data((unsigned char *)shm_data,
   CAIRO_FORMAT_RGB24, width, height, 4*width);
  cr = cairo_create(csurface);
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
  memset(video_ring_buf->buf, 0, video_ring_buf->size);
  setup_jack(dd);
  setup_signal_handler();
  while (wl_display_dispatch(display) != -1) {
  }
  close_stream(oc, dd->audio_thread_info->stream);
  close_stream(oc, dd->video_thread_info->stream);
  avio_closep(&oc->pb);
  avformat_free_context(oc);
  wl_display_disconnect(display);
  printf("disconnected from display\n");
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
  unsigned int min;
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
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR32; //replace
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

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
 uint32_t format, int fd, uint32_t size)
{
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
 uint32_t serial, struct wl_surface *surface,
 struct wl_array *keys)
{
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
 uint32_t serial, struct wl_surface *surface)
{
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
 uint32_t serial, uint32_t time, uint32_t key,
 uint32_t state)
{
  dingle_dots_t *dd = (dingle_dots_t *)data;
  static int first_r = 1;
  if (key == KEY_Q && state == 1 && (first_r != -1)) {
    exit(0);
  } else if (key == KEY_R && state == 1 && (first_r == 1)) {
    first_r = -1;
    start_recording_threads(dd);
  } else if (key == KEY_R && state == 1 && (first_r == -1)) {
    first_r = 0;
    recording_stopped = 1;
  }

}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
 uint32_t serial, uint32_t mods_depressed,
 uint32_t mods_latched, uint32_t mods_locked,
 uint32_t group)
{
  if (!shift_pressed && mods_depressed & 1) {
    shift_pressed = 1;
  } else if (shift_pressed && ((mods_depressed & 1) == 0)) {
    shift_pressed = 0;
  }
}

static const struct wl_keyboard_listener keyboard_listener = {
  keyboard_handle_keymap,
  keyboard_handle_enter,
  keyboard_handle_leave,
  keyboard_handle_key,
  keyboard_handle_modifiers,
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
 uint32_t serial, struct wl_surface *surface,
 wl_fixed_t sx, wl_fixed_t sy) {
}

static void pointer_leave(void *data,
    struct wl_pointer *wl_pointer, uint32_t serial,
    struct wl_surface *wl_surface) { }

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

static void pointer_motion(void *data,
    struct wl_pointer *wl_pointer, uint32_t time,
    wl_fixed_t surface_x, wl_fixed_t surface_y) {
  int i;
  dingle_dots_t *dd = (dingle_dots_t *)data;
  m_x = wl_fixed_to_int(surface_x);
  m_y = wl_fixed_to_int(surface_y);
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
}

static void pointer_button(void *data,
    struct wl_pointer *wl_pointer, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state)
{
  int i;
  dingle_dots_t * dd = (dingle_dots_t *)data;
  if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
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
  } else if (button == BTN_LEFT && state != WL_POINTER_BUTTON_STATE_PRESSED) {
    for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
      if (!dd->sound_shapes[i].active) continue;
      dd->sound_shapes[i].mdown = 0;
    }
  }
  if (!shift_pressed && button == BTN_RIGHT
   && state == WL_POINTER_BUTTON_STATE_PRESSED) {
    mdown = 1;
    mdown_x = m_x;
    mdown_y = m_y;
    FIRST_X = mdown_x;
    FIRST_Y = mdown_y;
    FIRST_W = 20 * ascale_factor_x;
    FIRST_H = 20 * ascale_factor_y;
  } else if (!shift_pressed && button == BTN_RIGHT
   && state != WL_POINTER_BUTTON_STATE_PRESSED) {
    mdown = 0;
    make_new_tld = 1;
    doing_tld = 1;
  }
  if (doing_tld && shift_pressed) {
    if (button == BTN_RIGHT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
      doing_tld = 0;
    }
  }
}

static void pointer_axis(void *data,
    struct wl_pointer *wl_pointer, uint32_t time,
    uint32_t axis, wl_fixed_t value) { }

static const struct wl_pointer_listener pointer_listener = {
  .enter = pointer_enter,
  .leave = pointer_leave,
  .motion = pointer_motion,
  .button = pointer_button,
  .axis = pointer_axis
};

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
  /*mysteriously, this does not work... video_st global variable does work.
  OutputStream video_st;*/
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
  setup_wayland(&dingle_dots);
  open_device();
  init_device();
  start_capturing();
  mainloop(&dingle_dots);
  stop_capturing();
  uninit_device();
  close_device();
  cleanup_wayland();
  fprintf(stderr, "\n");
  return 0;
}
