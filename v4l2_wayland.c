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
#include <linux/input.h>
#include <ccv/ccv.h>
#include <cairo/cairo.h>
#include <wayland-client.h>
#include <linux/videodev2.h>

#include "muxing.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define CLIPVALUE(v) ((v) < 255 ? (v) : 255)

struct buffer {
  void   *start;
  size_t  length;
};

typedef struct _thread_info {
  pthread_t thread_id;
} disk_thread_info_t;

static int read_frame(void);

output_frame                   out_frame;
OutputStream                   video_st, audio_st;
AVFormatContext                *oc;
AVFrame                        *frame;
char                           *out_file_name;
uint32_t                       width = 640;
uint32_t                       height = 360;
uint32_t                       ascale_factor = 2;
uint32_t                       awidth;
uint32_t                       aheight;
uint32_t                       stream_bitrate = 10000;
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
void *shm_data;

static const struct wl_registry_listener registry_listener;
static const struct wl_keyboard_listener keyboard_listener;
static const struct wl_pointer_listener pointer_listener;

static cairo_surface_t    *csurface;
static cairo_t            *cr;
static int                mdown = 0, mdown_x, mdown_y;
static int                m_x, m_y;
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
static disk_thread_info_t video_thread_info;
static disk_thread_info_t audio_thread_info;

volatile int              can_process;
volatile int              can_capture;
jack_client_t            *client;
jack_ringbuffer_t        *video_ring_buf, *audio_ring_buf;
unsigned int              nports = 2;
jack_port_t             **ports;
jack_default_audio_sample_t **in;
jack_nframes_t            nframes;
const size_t              sample_size = sizeof(jack_default_audio_sample_t);
pthread_mutex_t           audio_disk_thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t            audio_data_ready = PTHREAD_COND_INITIALIZER;
pthread_mutex_t           video_disk_thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t            video_data_ready = PTHREAD_COND_INITIALIZER;
long                      jack_overruns = 0;

static void errno_exit(const char *s)
{
  fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
  exit(EXIT_FAILURE);
}

static void set_ascale_factor(uint32_t factor) {
  ascale_factor = factor;
  awidth = width / ascale_factor;
  aheight = height / ascale_factor;
}

static int xioctl(int fh, int request, void *arg)
{
  int r;
  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);
  return r;
}

void setup_wayland() {
  struct wl_registry *registry;
  display = wl_display_connect(NULL);
  if (display == NULL) {
    fprintf(stderr, "Can't connect to display\n");
    exit(1);
  }
  printf("connected to display\n");
  registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
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
 printf("Got a registry event for %s id %d\n", interface, name);
 if (strcmp(interface, wl_compositor_interface.name) == 0)
    compositor = wl_registry_bind(registry, name,
        &wl_compositor_interface, min(version, 4));
  else if (strcmp(interface, wl_shm_interface.name) == 0)
    shm = wl_registry_bind(registry, name,
        &wl_shm_interface, min(version, 1));
  else if (strcmp(interface, wl_shell_interface.name) == 0)
    shell = wl_registry_bind(registry, name,
        &wl_shell_interface, min(version, 1));
  else if (strcmp(interface, wl_seat_interface.name) == 0) {
    seat = wl_registry_bind(registry, name,
        &wl_seat_interface, min(version, 2));
    pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(pointer, &pointer_listener, NULL);
    keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
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
  fprintf(stderr, "Painting pixels\n");
  for (n =0; n < width * height; n++) {
    *pixel++ = 0xffff00;
  }
}

static const struct wl_callback_listener frame_listener;

static void redraw(void *data, struct wl_callback *callback, uint32_t time)
{
  wl_callback_destroy(frame_callback);
  wl_surface_damage(surface, 0, 0,
      width, height); 
  read_frame();
  frame_callback = wl_surface_frame(surface);
  wl_surface_attach(surface, buffer, 0, 0);
  wl_callback_add_listener(frame_callback,
      &frame_listener, NULL);
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
  disk_thread_info_t *info = (disk_thread_info_t *)arg;
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  pthread_mutex_lock(&audio_disk_thread_lock);
  while(1) {
    ret = write_audio_frame(oc, &audio_st);
    if (ret == 1) {
      audio_done = 1;
      break;
    }
    if (ret == 0) continue;
    if (ret == -1) pthread_cond_wait(&audio_data_ready, &audio_disk_thread_lock);
  }
  if (audio_done && video_done && !trailer_written) {
    av_write_trailer(oc);
    trailer_written = 1;
  }
  pthread_mutex_unlock(&audio_disk_thread_lock);
  return 0;
}

void *video_disk_thread (void *arg) {
  static int out_done = 0;
  int ret;
  disk_thread_info_t *info = (disk_thread_info_t *)arg;
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  pthread_mutex_lock(&video_disk_thread_lock);
  while(1) {
    ret = write_video_frame(oc, &video_st);
    if (ret == 1) {
      video_done = 1;
      break;
    }
    if (ret == 0) continue;
    if (ret == -1) pthread_cond_wait(&video_data_ready, &video_disk_thread_lock);
  }
  if (audio_done && video_done && !trailer_written) {
    av_write_trailer(oc);
    trailer_written = 1;
  }
  pthread_mutex_unlock(&video_disk_thread_lock);
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

static void process_image(const void *p, int size)
{
  static int first_call = 1;
  unsigned char y0, y1, u, v;
  unsigned char r, g, b;
  int n;
  uint32_t *pixel = shm_data;
  unsigned char *ptr = (unsigned char*) p;
  if (video_done) return;
  for (n = 0; n < size; n += 4) {
    y0 = (unsigned char)ptr[n + 0];
    u = (unsigned char)ptr[n + 1];
    y1 = (unsigned char)ptr[n + 2];
    v = (unsigned char)ptr[n + 3];
    YUV2RGB(y0, u, v, &r, &g, &b);
    *pixel++ = r << 16 | g << 8 | b;
    YUV2RGB(y1, u, v, &r, &g, &b);
    *pixel++ = r << 16 | g << 8 | b;
  }
  if (mdown) {
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.5, 0., 0., 0.5);
    cairo_rectangle(cr, FIRST_X, FIRST_Y, FIRST_W, FIRST_H);
    cairo_fill(cr);
    cairo_restore(cr);
  }
  if (doing_tld) {
    int ret = sws_scale(resize, (uint8_t const * const *)frame->data, frame->linesize, 0, frame->height,
    aframe->data, aframe->linesize);
    if (make_new_tld == 1) {
        printf("%d %d %d %d\n", FIRST_X/ascale_factor, FIRST_Y/ascale_factor, FIRST_W/ascale_factor, FIRST_H/ascale_factor);
      if (FIRST_W > 0 && FIRST_H > 0) {
        tld = new_tld(FIRST_X/ascale_factor, FIRST_Y/ascale_factor, FIRST_W/ascale_factor, FIRST_H/ascale_factor);
      }
      make_new_tld = 0;
    } else {
      ccv_read(aframe->data[0], &cdm2, CCV_IO_ARGB_RAW | CCV_IO_GRAY, aheight, awidth, 4*awidth);
      ccv_tld_info_t info;
      ccv_comp_t newbox = ccv_tld_track_object(tld, cdm, cdm2, &info);
      if (tld->found) {
        printf("FOUND\n");
        cairo_set_source_rgba(cr, 0., 0.25, 0.5, 0.5);
        cairo_rectangle(cr, ascale_factor*newbox.rect.x, ascale_factor*newbox.rect.y, ascale_factor*newbox.rect.width,
            ascale_factor*newbox.rect.height);
        cairo_fill(cr);
        cdm = cdm2;
        cdm2 = 0;
      } else {
        cdm = cdm2;
        cdm2 = 0;
        printf("NOT FOUND\n");
      }
    }
  }
  if (!recording_started || recording_stopped) return;
  if (first_call) {
    first_call = 0;
    video_st.first_time = out_frame.ts;
  }
  int tsize = out_frame.size + sizeof(struct timespec);
  //memcpy(out_frame.data, shm_data, out_frame.size);
  if (jack_ringbuffer_write_space(video_ring_buf) >= tsize) {
    if (jack_ringbuffer_write(video_ring_buf, (void *)shm_data, out_frame.size) <
     out_frame.size ||
     jack_ringbuffer_write(video_ring_buf, (void *)&out_frame.ts, sizeof(struct timespec)) <
     sizeof(struct timespec)) {
      video_st.overruns++;
      printf("overruns: %d\n", video_st.overruns);
    } else {
      printf("was able to write to ringbuffer\n");
    }
  } else {
    video_st.overruns++;
    printf("overruns: %d\n", video_st.overruns);
  }
  if (pthread_mutex_trylock (&video_disk_thread_lock) == 0) {
    pthread_cond_signal (&video_data_ready);
    pthread_mutex_unlock (&video_disk_thread_lock);
  }
}

static int read_frame(void)
{
  struct v4l2_buffer buf;
  unsigned int i;
  CLEAR(buf);
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
    switch (errno) {
      case EAGAIN:
        printf("EAGAIN\n");
        return 0;
      case EIO:
        /* Could ignore EIO, see spec. */
        /* fall through */
      default:
        errno_exit("VIDIOC_DQBUF");
    }
  }
  assert(buf.index < n_buffers);
  clock_gettime(CLOCK_MONOTONIC, &out_frame.ts);
  process_image(buffers[buf.index].start, buf.bytesused);
  if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
    errno_exit("VIDIOC_QBUF");
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

int process(jack_nframes_t nframes, void *arg) {
  int chn;
  size_t i;
  if ((!can_process) || (!can_capture) || (audio_done)) return 0;
  for (chn = 0; chn < nports; chn++)
    in[chn] = jack_port_get_buffer(ports[chn], nframes);
  for (i = 0; i < nframes; i++) {
    for (chn = 0; chn < nports; chn++) {
      if (jack_ringbuffer_write (audio_ring_buf, (void *) (in[chn]+i),
       sample_size) < sample_size) {
        printf("jack overrun: %d\n", ++jack_overruns);
      }
    }
  }
  if (pthread_mutex_trylock (&audio_disk_thread_lock) == 0) {
    pthread_cond_signal (&audio_data_ready);
    pthread_mutex_unlock (&audio_disk_thread_lock);
  }
  return 0;
}

void jack_shutdown (void *arg) {
  printf("JACK shutdown\n");
  abort();
}

void setup_jack() {
  unsigned int i;
  size_t in_size;
  can_process = 0;
  if ((client = jack_client_open("v4l2_wayland", JackNoStartServer, NULL)) == 0) {
    printf("jack server not running?\n");
    exit(1);
  }
  jack_set_process_callback(client, process, NULL);
  jack_on_shutdown(client, jack_shutdown, NULL);
  if (jack_activate(client)) {
    printf("cannot activate jack client\n");
  }
  ports = (jack_port_t **) malloc(sizeof(jack_port_t *) * nports);
  in_size =  nports * sizeof (jack_default_audio_sample_t *);
  in = (jack_default_audio_sample_t **) malloc (in_size);
  audio_ring_buf = jack_ringbuffer_create (nports * sample_size *
   16384);
  memset(in, 0, in_size);
  memset(audio_ring_buf->buf, 0, audio_ring_buf->size);
  for (i = 0; i < nports; i++) {
    char name[64];
    sprintf(name, "input%d", i + 1);
    if ((ports[i] = jack_port_register (client, name, JACK_DEFAULT_AUDIO_TYPE,
     JackPortIsInput, 0)) == 0) {
      printf("cannot register input port \"%s\"!\n", name);
      jack_client_close(client);
      exit(1);
    }
  }
  can_process = 1;
}
void start_recording_threads() {
  recording_started = 1;
  printf("start_recording\n");
  pthread_create(&audio_thread_info.thread_id, NULL, audio_disk_thread,
   &audio_thread_info);
  pthread_create(&video_thread_info.thread_id, NULL, video_disk_thread,
   &video_thread_info);
}

static void mainloop(void) {
  int ret;
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
  wl_callback_add_listener(frame_callback, &frame_listener, NULL);
  create_window();
  ccv_enable_default_cache();
  FIRST_W = width/5.;
  FIRST_H = height/5.;
  FIRST_X = width/2.0;
  FIRST_Y = height/2.0 - 0.5 * FIRST_H;
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
  cairo_set_source_rgba(cr, 0., 0.25, 0.5, 0.5);
  init_output();//make_new_tld = 1;
  out_frame.size = 4 * width * height;
  out_frame.data = calloc(1, out_frame.size);
  uint32_t rb_size = 200 * 4 * 640 * 360 / out_frame.size;
  printf("rb_size: %d\n", rb_size);
  video_ring_buf = jack_ringbuffer_create(rb_size*out_frame.size);
  memset(video_ring_buf->buf, 0, video_ring_buf->size);
  setup_jack();
  while (wl_display_dispatch(display) != -1) {
  //  printf("in wl_display_dispatch\n");
  }
  av_write_trailer(oc);
  /* Close each codec. */
  close_stream(oc, &video_st);
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
  fprintf(stderr, "Set format\r\n");
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

void hello_free_cursor(void)
{
  wl_pointer_set_user_data(pointer, NULL);
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
  static int first_r = 1;
  if (key == KEY_R && state == 1 && (first_r == 1)) {
    first_r = -1;
    start_recording_threads();
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
}

static const struct wl_keyboard_listener keyboard_listener = {
  keyboard_handle_keymap,
  keyboard_handle_enter,
  keyboard_handle_leave,
  keyboard_handle_key,
  keyboard_handle_modifiers,
};

static void pointer_enter(void *data,
    struct wl_pointer *wl_pointer,
    uint32_t serial, struct wl_surface *surface,
    wl_fixed_t surface_x, wl_fixed_t surface_y)
{
}

static void pointer_leave(void *data,
    struct wl_pointer *wl_pointer, uint32_t serial,
    struct wl_surface *wl_surface) { }

static void pointer_motion(void *data,
    struct wl_pointer *wl_pointer, uint32_t time,
    wl_fixed_t surface_x, wl_fixed_t surface_y) {
  m_x = wl_fixed_to_int(surface_x);
  m_y = wl_fixed_to_int(surface_y);
  if (mdown) {
    FIRST_W = m_x - mdown_x;
    FIRST_H = m_y - mdown_y;
    if (FIRST_W < 0) {
      FIRST_X = mdown_x + FIRST_W;
      FIRST_W = -FIRST_W;
    }
    if (FIRST_H < 0) {
      FIRST_Y = mdown_y + FIRST_H;
      FIRST_H = -FIRST_H;
    }
  } else {
    //FIRST_W = FIRST_H = 0;
  }
}

static void pointer_button(void *data,
    struct wl_pointer *wl_pointer, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state)
{
  void (*callback)(uint32_t);

  if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
    mdown = 1;
    mdown_x = m_x;
    mdown_y = m_y;
    FIRST_X = mdown_x;
    FIRST_Y = mdown_y;
    FIRST_W = 0;
    FIRST_H = 0;
  } else if (button == BTN_LEFT && state != WL_POINTER_BUTTON_STATE_PRESSED) {
    mdown = 0;
    FIRST_W = m_x - mdown_x;
    FIRST_H = m_y - mdown_y;
    if (FIRST_W < 0) {
      FIRST_X = mdown_x + FIRST_W;
      FIRST_W = -FIRST_W;
    }
    if (FIRST_H < 0) {
      FIRST_Y = mdown_y + FIRST_H;
      FIRST_H = -FIRST_H;
    }
    printf ("%d %d %d %d\n", FIRST_X, FIRST_Y, FIRST_W, FIRST_H);
    make_new_tld = 1;
    doing_tld = 1;
  } else if (button == BTN_RIGHT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
    doing_tld = 0;
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
      argv[0], dev_name, frame_count);
}

static const char short_options[] = "d:ho:b:w:g:f:";

static const struct option
long_options[] = {
  { "device", required_argument, NULL, 'd' },
  { "help",   no_argument,       NULL, 'h' },
  { "output", required_argument, NULL, 'o' },
  { "bitrate", required_argument, NULL, 'b' },
  { "width", required_argument, NULL, 'w' },
  { "height", required_argument, NULL, 'g' },
  { "ascale_factor", required_argument, NULL, 'f' },
  { 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
  out_file_name = "testington.webm";
  dev_name = "/dev/video0";
  set_ascale_factor(ascale_factor);
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
        printf("out_file_name: <%s>\n", out_file_name);
        break;
      case 'b':
        stream_bitrate = atoi(optarg);
        printf("bitrate: <%d>\n", stream_bitrate);
        break;
      case 'w':
        width = atoi(optarg);
        awidth = width / ascale_factor;
        break;
      case 'g':
        height = atoi(optarg);
        aheight = width / ascale_factor;
        break;
      case 'f':
        set_ascale_factor(atoi(optarg));
        break;
      case 'h':
        usage(stdout, argc, argv);
        exit(EXIT_SUCCESS);
      default:
        usage(stderr, argc, argv);
        exit(EXIT_FAILURE);
    }
  }
  setup_wayland();
  open_device();
  init_device();
  start_capturing();
  mainloop();
  stop_capturing();
  uninit_device();
  close_device();
  cleanup_wayland();
  fprintf(stderr, "\n");
  return 0;
}
