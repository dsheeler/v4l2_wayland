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

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <ccv/ccv.h>
#include <cairo/cairo.h>
#include <wayland-client.h>
#include <linux/videodev2.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define CLIPVALUE(v) ((v) < 255 ? (v) : 255)

enum io_method {
  IO_METHOD_READ,
  IO_METHOD_MMAP,
  IO_METHOD_USERPTR,
};

struct buffer {
  void   *start;
  size_t  length;
};

static int read_frame(void);

static ccv_dense_matrix_t      *cdm = 0, *cdm2 = 0;
static ccv_tld_t               *tld = 0;
static struct wl_compositor    *compositor;
static struct wl_display       *display;
static struct wl_pointer       *pointer;
static struct wl_seat          *seat;
static struct wl_shell         *shell;
static struct wl_shell_surface *shell_surface;
static struct wl_shm           *shm;
static struct wl_surface       *surface;
static struct wl_buffer        *buffer;
static struct wl_callback      *frame_callback;
static const int WIDTH = 1280;
static const int HEIGHT = 720;
void *shm_data;

static const struct wl_registry_listener registry_listener;
static const struct wl_pointer_listener pointer_listener;

static struct pollfd    fds[1];
static char            *dev_name;
static enum io_method   io = IO_METHOD_MMAP;
static int              fd = -1;
struct buffer          *buffers;
static unsigned int     n_buffers;
static int              out_buf;
static int              force_format;
static int              frame_count = 200;
static int              frame_number = 0;

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
  for (n =0; n < WIDTH*HEIGHT; n++) {
    *pixel++ = 0xffff00;
  }
}

static const struct wl_callback_listener frame_listener;

static void redraw(void *data, struct wl_callback *callback, uint32_t time)
{
  fprintf(stderr, "Redrawing\n");   
  wl_callback_destroy(frame_callback);
  wl_surface_damage(surface, 0, 0,
      WIDTH, HEIGHT); 
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
  buffer = create_buffer(WIDTH, HEIGHT);
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

static void process_image(const void *p, int size)
{
  /*printf("memcpy get called\n");
  memcpy(shm_data, p, size);*/
  static int first_call = 1;
  unsigned char y0, y1, u, v;
  unsigned char r, g, b;
  int n;
  uint32_t *pixel = shm_data;
  unsigned char *ptr = (unsigned char*) p;
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
  if (first_call) {
    first_call = 0;
    int FIRST_W, FIRST_H, FIRST_X, FIRST_Y;
    FIRST_W = WIDTH/5.;
    FIRST_H = HEIGHT/5.;
    FIRST_X = WIDTH/2.0;
    FIRST_Y = HEIGHT/2.0 - 0.5 * FIRST_H;
    printf("%d %d %d %d\n", FIRST_X, FIRST_Y, FIRST_W, FIRST_H);
    ccv_rect_t box = ccv_rect(FIRST_X, FIRST_Y, FIRST_W, FIRST_Y);
    ccv_read(shm_data, &cdm, CCV_IO_ARGB_RAW | CCV_IO_GRAY, HEIGHT, WIDTH, 4*WIDTH);
    tld = ccv_tld_new(cdm, box, ccv_tld_default_params);
  }
  ccv_read(shm_data, &cdm2, CCV_IO_ARGB_RAW | CCV_IO_GRAY, HEIGHT, WIDTH, 4*WIDTH);
  ccv_tld_info_t info;
  ccv_comp_t newbox = ccv_tld_track_object(tld, cdm, cdm2, &info);
  cairo_surface_t *csurface;
  csurface = cairo_image_surface_create_for_data((unsigned char *)shm_data,
      CAIRO_FORMAT_RGB24, WIDTH, HEIGHT, 4*WIDTH);
  cairo_t *cr;
  cr = cairo_create(csurface);
  if (tld->found) {
    printf("found!\n");
    cairo_set_source_rgba(cr, 0., 0.25, 0., 0.5);
    cairo_rectangle(cr, newbox.rect.x, newbox.rect.y, newbox.rect.width,
     newbox.rect.height);
    cairo_close_path(cr);
    cairo_fill(cr);
  }
  cairo_surface_finish(csurface);
  cairo_destroy(cr);
  cdm = cdm2;
  cdm2 = 0;
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
        return 0;
      case EIO:
        /* Could ignore EIO, see spec. */
        /* fall through */
      default:
        errno_exit("VIDIOC_DQBUF");
    }
  }
  assert(buf.index < n_buffers);
  printf("buf.bytesused: %d\n", buf.bytesused);
  process_image(buffers[buf.index].start, buf.bytesused);
  if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
    errno_exit("VIDIOC_QBUF");
  return 1;
}

static void mainloop(void) {
  /*unsigned int count;
  count = frame_count;
  while (count-- > 0) {
    for (;;) {
      int r;
      fds[0].fd = fd;
      fds[0].events = POLLIN;
      r = poll(fds, 1, 2000);
      if (r > 0) {
        if (fds[0].revents & POLLIN) {
          if (read_frame())
            break;
        }
      }
      if (r < 0) {
        if (errno == EINTR || errno == EAGAIN)
          continue;
        errno_exit("select");
      }
      if (0 == r) {
        fprintf(stderr, "select timeout\n");
        exit(EXIT_FAILURE);
      }
    }
  }*/
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
  read_frame();
  while (wl_display_dispatch(display) != -1) {
    printf("in wl_display_dispatch\n");
  }
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
  fmt.fmt.pix.width       = WIDTH; //replace
  fmt.fmt.pix.height      = HEIGHT; //replace
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

static void usage(FILE *fp, int argc, char **argv)
{
  fprintf(fp,
      "Usage: %s [options]\n\n"
      "Version 1.3\n"
      "Options:\n"
      "-d | --device name   Video device name [%s]\n"
      "-h | --help          Print this message\n"
      "-m | --mmap          Use memory mapped buffers [default]\n"
      "-r | --read          Use read() calls\n"
      "-u | --userp         Use application allocated buffers\n"
      "-o | --output        Outputs stream to stdout\n"
      "-f | --format        Force format to 640x480 YUYV\n"
      "-c | --count         Number of frames to grab [%i]\n"
      "",
      argv[0], dev_name, frame_count);
}

static const char short_options[] = "d:hmruofc:";

static const struct option
long_options[] = {
  { "device", required_argument, NULL, 'd' },
  { "help",   no_argument,       NULL, 'h' },
  { "mmap",   no_argument,       NULL, 'm' },
  { "read",   no_argument,       NULL, 'r' },
  { "userp",  no_argument,       NULL, 'u' },
  { "output", no_argument,       NULL, 'o' },
  { "format", no_argument,       NULL, 'f' },
  { "count",  required_argument, NULL, 'c' },
  { 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
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
                case 'h':
                        usage(stdout, argc, argv);
                        exit(EXIT_SUCCESS);
                case 'm':
                        io = IO_METHOD_MMAP;
                        break;
                case 'r':
                        io = IO_METHOD_READ;
                        break;
                case 'u':
                        io = IO_METHOD_USERPTR;
                        break;
                case 'o':
                        out_buf++;
                        break;
                case 'f':
                        force_format++;
                        break;
                case 'c':
                        errno = 0;
                        frame_count = strtol(optarg, NULL, 0);
                        if (errno)
                                errno_exit(optarg);
                        break;
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
