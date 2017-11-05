#include "v4l2.h"

static int xioctl(int fh, int request, void *arg) {
  int r;
  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);
  return r;
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

int dd_v4l2_read_frames(dd_v4l2_t *v4l2) {
  struct v4l2_buffer buf;
	struct timespec ts;
  unsigned char y0, y1, u, v;
  unsigned char r, g, b;
	unsigned char *ptr;
	int i, j, nij;
	int n;
	for (;;) {
		poll(v4l2->pfd, 1, -1);
		CLEAR(buf);
	  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	  buf.memory = V4L2_MEMORY_MMAP;
	  if (!v4l2->active) continue;
		if (-1 == xioctl(v4l2->fd, VIDIOC_DQBUF, &buf)) {
	    switch (errno) {
	      case EAGAIN:
	        break;
	      case EIO:
	        /* Could ignore EIO, see spec. */
	        /* fall through */
	      default:
	        errno_exit("VIDIOC_DQBUF");
	    }
	  }
		ptr = (unsigned char *)v4l2->buffers[buf.index].start;
		for (n = 0; n < 2*v4l2->width*v4l2->height; n += 4) {
			nij = (int) n / 2;
			i = nij%v4l2->width;
			j = nij/v4l2->width;
			y0 = (unsigned char)ptr[n + 0];
			u = (unsigned char)ptr[n + 1];
			y1 = (unsigned char)ptr[n + 2];
			v = (unsigned char)ptr[n + 3];
			YUV2RGB(y0, u, v, &r, &g, &b);
			v4l2->save_buf[v4l2->width - 1 - i + j*v4l2->width] = 255 << 24 | r << 16 | g << 8 | b;
			YUV2RGB(y1, u, v, &r, &g, &b);
			v4l2->save_buf[v4l2->width - 1 - (i+1) + j*v4l2->width] = 255 << 24 | r << 16 | g << 8 | b;
		}
		assert(buf.index < v4l2->n_buffers);
		clock_gettime(CLOCK_MONOTONIC, &ts);
		int space = 4 * v4l2->width * v4l2->height + sizeof(struct timespec);
		int buf_space = jack_ringbuffer_write_space(v4l2->rbuf);
		while (buf_space < space) {
			pthread_cond_wait(&v4l2->data_ready, &v4l2->lock);
			buf_space = jack_ringbuffer_write_space(v4l2->rbuf);
		}
		jack_ringbuffer_write(v4l2->rbuf, (void *)&ts, sizeof(struct timespec));
		jack_ringbuffer_write(v4l2->rbuf, (void *)v4l2->save_buf, 4 * v4l2->width * v4l2->height);
		if (-1 == xioctl(v4l2->fd, VIDIOC_QBUF, &buf))
			errno_exit("VIDIOC_QBUF");
	}
}

void dd_v4l2_stop_capturing(dd_v4l2_t *v) {
  enum v4l2_buf_type type;
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(v->fd, VIDIOC_STREAMOFF, &type))
    errno_exit("VIDIOC_STREAMOFF");
}

void dd_v4l2_start_capturing(dd_v4l2_t *v) {
  unsigned int i;
  enum v4l2_buf_type type;
  for (i = 0; i < v->n_buffers; ++i) {
    struct v4l2_buffer buf;
    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (-1 == xioctl(v->fd, VIDIOC_QBUF, &buf))
      errno_exit("VIDIOC_QBUF");
  }
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(v->fd, VIDIOC_STREAMON, &type))
    errno_exit("VIDIOC_STREAMON");
}

void dd_v4l2_uninit_device(dd_v4l2_t *v) {
  unsigned int i;
  for (i = 0; i < v->n_buffers; ++i)
    if (-1 == munmap(v->buffers[i].start, v->buffers[i].length))
      errno_exit("munmap");
  free(v->buffers);
}

void dd_v4l2_init_mmap(dd_v4l2_t *v) {
  struct v4l2_requestbuffers req;
  CLEAR(req);
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (-1 == xioctl(v->fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s does not support "
          "memory mapping\n", v->dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_REQBUFS");
    }
  }
  if (req.count < 2) {
    fprintf(stderr, "Insufficient buffer memory on %s\n",
        v->dev_name);
    exit(EXIT_FAILURE);
  }
  v->buffers = calloc(req.count, sizeof(*v->buffers));
  if (!v->buffers) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
  for (v->n_buffers = 0; v->n_buffers < req.count; ++v->n_buffers) {
    struct v4l2_buffer buf;
    CLEAR(buf);
    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory      = V4L2_MEMORY_MMAP;
    buf.index       = v->n_buffers;
    if (-1 == xioctl(v->fd, VIDIOC_QUERYBUF, &buf))
      errno_exit("VIDIOC_QUERYBUF");
    v->buffers[v->n_buffers].length = buf.length;
    v->buffers[v->n_buffers].start =
      mmap(NULL /* start anywhere */,
          buf.length,
          PROT_READ | PROT_WRITE /* required */,
          MAP_SHARED /* recommended */,
          v->fd, buf.m.offset);
    if (MAP_FAILED == v->buffers[v->n_buffers].start)
      errno_exit("mmap");
  }
}


void dd_v4l2_init_device(dd_v4l2_t *v) {
  struct v4l2_capability cap;
  struct v4l2_cropcap cropcap;
  struct v4l2_crop crop;
  struct v4l2_format fmt;
  if (-1 == xioctl(v->fd, VIDIOC_QUERYCAP, &cap)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s is no V4L2 device\n",
          v->dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_QUERYCAP");
    }
  }
  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf(stderr, "%s is no video capture device\n",
        v->dev_name);
    exit(EXIT_FAILURE);
  }
  if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
    fprintf(stderr, "%s does not support streaming i/o\n",
        v->dev_name);
    exit(EXIT_FAILURE);
  }
  /* Select video input, video standard and tune here. */
  CLEAR(cropcap);
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (0 == xioctl(v->fd, VIDIOC_CROPCAP, &cropcap)) {
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect; /* reset to default */
    if (-1 == xioctl(v->fd, VIDIOC_S_CROP, &crop)) {
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
  fmt.fmt.pix.width       = v->width;
  fmt.fmt.pix.height      = v->height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  fmt.fmt.pix.field       = V4L2_FIELD_ANY;
  if (-1 == xioctl(v->fd, VIDIOC_S_FMT, &fmt))
    errno_exit("VIDIOC_S_FMT");
  /* Note VIDIOC_S_FMT may change width and height. */
  /* Buggy driver paranoia. */
  /*min = fmt.fmt.pix.width * 2;
  if (fmt.fmt.pix.bytesperline < min)
    fmt.fmt.pix.bytesperline = min;
  min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
  if (fmt.fmt.pix.sizeimage < min)
    fmt.fmt.pix.sizeimage = min;*/
  dd_v4l2_init_mmap(v);
}

void dd_v4l2_close_device(dd_v4l2_t *v) {
  if (-1 == close(v->fd))
    errno_exit("close");
  v->fd = -1;
}

void dd_v4l2_open_device(dd_v4l2_t *v) {
	struct stat st;
  if (-1 == stat(v->dev_name, &st)) {
    fprintf(stderr, "Cannot identify '%s': %d, %s\n",
        v->dev_name, errno, strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (!S_ISCHR(st.st_mode)) {
    fprintf(stderr, "%s is no device\n", v->dev_name);
    exit(EXIT_FAILURE);
  }
  v->fd = open(v->dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);
  if (-1 == v->fd) {
    fprintf(stderr, "Cannot open '%s': %d, %s\n",
        v->dev_name, errno, strerror(errno));
    exit(EXIT_FAILURE);
  }
	memset(&v->pfd, 0, sizeof(struct pollfd));
	v->pfd->fd = v->fd;
	v->pfd->events = POLLIN;
}

int dd_v4l2_in(dd_v4l2_t *v, double x, double y) {
	if ((x >= ((draggable *)v)->pos.x && x <= v->dr.pos.x + v->width) &&
			(y >= v->dr.pos.y && y <= v->dr.pos.y + v->height)) {
		return 1;
	} else {
		return 0;
	}
}
