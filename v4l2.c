#include "dingle_dots.h"
#include "v4l2.h"

V4l2::V4l2() { }

int V4l2::xioctl(int fh, int request, void *arg) {
  int r;
  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);
  return r;
}

void V4l2::YUV2RGB(const unsigned char y, const unsigned char u,
 const unsigned char v, unsigned char* r,
 unsigned char* g, unsigned char* b) {
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

void V4l2::create(DingleDots *dd, char *dev_name, double width, double height, uint64_t z) {
	memset(this, 0, sizeof(*this));
	this->dd = dd;
	strncpy(this->dev_name, dev_name, DD_V4L2_MAX_STR_LEN-1);
  this->z = z;
	this->pos.x = 0;
	this->pos.y = 0;
  this->width = width;
  this->height = height;
	pthread_create(&this->thread_id, NULL, V4l2::thread, this);
}

void *V4l2::thread(void *arg) {
  V4l2 *v = (V4l2 *)arg;
	v->rbuf = jack_ringbuffer_create(5*4*v->width*v->height);
	memset(v->rbuf->buf, 0, v->rbuf->size);
	v->read_buf = (uint32_t *)(malloc(4 * v->width * v->height));
	v->save_buf = (uint32_t *)(malloc(4 * v->width * v->height));
	v->active = 1;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	v->open_device();
  v->init_device();
  v->start_capturing();
  pthread_mutex_lock(&v->lock);
	v->read_frames();
  pthread_mutex_unlock(&v->lock);
  v->stop_capturing();
  v->uninit_device();
  v->close_device();
	return 0;
}

int V4l2::read_frames() {
  struct v4l2_buffer buf;
	struct timespec ts;
  unsigned char y0, y1, u, v;
  unsigned char r, g, b;
	unsigned char *ptr;
	int i, j, nij;
	int n;
	for (;;) {
		poll(this->pfd, 1, -1);
		gtk_widget_queue_draw(this->dd->drawing_area);
		CLEAR(buf);
	  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	  buf.memory = V4L2_MEMORY_MMAP;
	  if (!this->active) continue;
		if (-1 == xioctl(this->fd, VIDIOC_DQBUF, &buf)) {
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
		ptr = (unsigned char *)this->buffers[buf.index].start;
		for (n = 0; n < 2*this->width*this->height; n += 4) {
			nij = (int) n / 2;
			i = nij%this->width;
			j = nij/this->width;
			y0 = (unsigned char)ptr[n + 0];
			u = (unsigned char)ptr[n + 1];
			y1 = (unsigned char)ptr[n + 2];
			v = (unsigned char)ptr[n + 3];
			YUV2RGB(y0, u, v, &r, &g, &b);
			this->save_buf[this->width - 1 - i + j*this->width] = 255 << 24 | r << 16 | g << 8 | b;
			YUV2RGB(y1, u, v, &r, &g, &b);
			this->save_buf[this->width - 1 - (i+1) + j*this->width] = 255 << 24 | r << 16 | g << 8 | b;
		}
		assert(buf.index < this->n_buffers);
		clock_gettime(CLOCK_MONOTONIC, &ts);
		int space = 4 * this->width * this->height + sizeof(struct timespec);
		int buf_space = jack_ringbuffer_write_space(this->rbuf);
		while (buf_space < space) {
			pthread_cond_wait(&this->data_ready, &this->lock);
			buf_space = jack_ringbuffer_write_space(this->rbuf);
		}
		jack_ringbuffer_write(this->rbuf, (const char *)&ts,
		 sizeof(struct timespec));
		jack_ringbuffer_write(this->rbuf, (const char *)this->save_buf,
		 4 * this->width * this->height);
		if (-1 == xioctl(this->fd, VIDIOC_QBUF, &buf))
			errno_exit("VIDIOC_QBUF");
	}
}

void V4l2::stop_capturing() {
  enum v4l2_buf_type type;
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(this->fd, VIDIOC_STREAMOFF, &type))
    errno_exit("VIDIOC_STREAMOFF");
}

void V4l2::start_capturing() {
  unsigned int i;
  enum v4l2_buf_type type;
  for (i = 0; i < this->n_buffers; ++i) {
    struct v4l2_buffer buf;
    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (-1 == xioctl(this->fd, VIDIOC_QBUF, &buf))
      errno_exit("VIDIOC_QBUF");
  }
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(this->fd, VIDIOC_STREAMON, &type))
    errno_exit("VIDIOC_STREAMON");
}

void V4l2::uninit_device() {
  unsigned int i;
  for (i = 0; i < this->n_buffers; ++i)
    if (-1 == munmap(this->buffers[i].start, this->buffers[i].length))
      errno_exit("munmap");
  free(this->buffers);
}

void V4l2::init_mmap() {
  struct v4l2_requestbuffers req;
  CLEAR(req);
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (-1 == xioctl(this->fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s does not support "
          "memory mapping\n", this->dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_REQBUFS");
    }
  }
  if (req.count < 2) {
    fprintf(stderr, "Insufficient buffer memory on %s\n",
        this->dev_name);
    exit(EXIT_FAILURE);
  }
  this->buffers = (struct dd_v4l2_buffer *)calloc(req.count,
	 sizeof(*this->buffers));
  if (!this->buffers) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
  for (this->n_buffers = 0; this->n_buffers < req.count; ++this->n_buffers) {
    struct v4l2_buffer buf;
    CLEAR(buf);
    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory      = V4L2_MEMORY_MMAP;
    buf.index       = this->n_buffers;
    if (-1 == xioctl(this->fd, VIDIOC_QUERYBUF, &buf))
      errno_exit("VIDIOC_QUERYBUF");
    this->buffers[this->n_buffers].length = buf.length;
    this->buffers[this->n_buffers].start =
      mmap(NULL /* start anywhere */,
     	 buf.length,
       PROT_READ | PROT_WRITE /* required */,
       MAP_SHARED /* recommended */,
       this->fd, buf.m.offset);
    if (MAP_FAILED == this->buffers[this->n_buffers].start)
      errno_exit("mmap");
  }
}


void V4l2::init_device() {
  struct v4l2_capability cap;
  struct v4l2_cropcap cropcap;
  struct v4l2_crop crop;
  struct v4l2_format fmt;
  if (-1 == xioctl(this->fd, VIDIOC_QUERYCAP, &cap)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s is no V4L2 device\n",
          this->dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_QUERYCAP");
    }
  }
  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf(stderr, "%s is no video capture device\n",
        this->dev_name);
    exit(EXIT_FAILURE);
  }
  if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
    fprintf(stderr, "%s does not support streaming i/o\n",
        this->dev_name);
    exit(EXIT_FAILURE);
  }
  /* Select video input, video standard and tune here. */
  CLEAR(cropcap);
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (0 == xioctl(this->fd, VIDIOC_CROPCAP, &cropcap)) {
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect; /* reset to default */
    if (-1 == xioctl(this->fd, VIDIOC_S_CROP, &crop)) {
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
  fmt.fmt.pix.width       = this->width;
  fmt.fmt.pix.height      = this->height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  fmt.fmt.pix.field       = V4L2_FIELD_ANY;
  if (-1 == xioctl(this->fd, VIDIOC_S_FMT, &fmt))
    errno_exit("VIDIOC_S_FMT");
  /* Note VIDIOC_S_FMT may change width and height. */
  /* Buggy driver paranoia. */
  /*min = fmt.fmt.pix.width * 2;
  if (fmt.fmt.pix.bytesperline < min)
    fmt.fmt.pix.bytesperline = min;
  min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
  if (fmt.fmt.pix.sizeimage < min)
    fmt.fmt.pix.sizeimage = min;*/
  this->init_mmap();
}

void V4l2::close_device() {
  if (-1 == close(this->fd))
    errno_exit("close");
  this->fd = -1;
}

void V4l2::open_device() {
	struct stat st;
  if (-1 == stat(this->dev_name, &st)) {
    fprintf(stderr, "Cannot identify '%s': %d, %s\n",
        this->dev_name, errno, strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (!S_ISCHR(st.st_mode)) {
    fprintf(stderr, "%s is no device\n", this->dev_name);
    exit(EXIT_FAILURE);
  }
  this->fd = open(this->dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);
  if (-1 == this->fd) {
    fprintf(stderr, "Cannot open '%s': %d, %s\n",
        this->dev_name, errno, strerror(errno));
    exit(EXIT_FAILURE);
  }
	memset(&this->pfd, 0, sizeof(struct pollfd));
	this->pfd->fd = this->fd;
	this->pfd->events = POLLIN;
}

int V4l2::in(double x, double y) {
	if ((x >= this->pos.x && x <= this->pos.x + this->width) &&
			(y >= this->pos.y && y <= this->pos.y + this->height)) {
		return 1;
	} else {
		return 0;
	}
}

void *dd_v4l2_thread(void *arg) {
  V4l2 *v = (V4l2 *)arg;
	v->rbuf = jack_ringbuffer_create(5*4*v->width*v->height);
	memset(v->rbuf->buf, 0, v->rbuf->size);
	v->read_buf = (uint32_t *)(malloc(4 * v->width * v->height));
	v->save_buf = (uint32_t *)(malloc(4 * v->width * v->height));
	v->active = 1;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	v->open_device();
  v->init_device();
  v->start_capturing();
  pthread_mutex_lock(&v->lock);
	v->read_frames();
  pthread_mutex_unlock(&v->lock);
  v->stop_capturing();
  v->uninit_device();
  v->close_device();
	return 0;
}


