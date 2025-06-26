#include <utility>
#include <map>
#include <iostream>
#include <fstream>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include "dingle_dots.h"
#include "v4l2.h"

V4l2::V4l2() { active = 0; allocated = 0; }

int V4l2::xioctl(int fh, int request, void *arg) {
	int r;
	do {
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);
	return r;
}

static int check_new_frame_ready(gpointer data) {
	V4l2 *v = (V4l2 *)data;
	if (v->is_done()) {
		return 0;
	} else if (v->is_new_frame_ready()) {
		v->dingle_dots->queue_draw();
	}
	return 1;
}

void V4l2::init(DingleDots *dd, char *dev_name, double width, double height, bool mirrored, uint64_t z) {
	this->allocating = 1;
    this->dingle_dots = dd;
	strncpy(this->dev_name, dev_name, DD_V4L2_MAX_STR_LEN-1);
	this->z = z;
	this->pos.x = 0;
	this->pos.y = 0;
	this->active = 0;
	this->finished = 0;
	this->selected = 0;
	this->mdown = 0;
	this->allocated = 0;
	this->new_frame_ready = False;
	this->pos.width = width;
	this->pos.height = height;
	this->mirrored = mirrored;
	this->hovered = False;
	g_timeout_add(10, check_new_frame_ready, this);
	pthread_create(&this->thread_id, nullptr, V4l2::thread, this);

}

void V4l2::uninit()
{
	this->finished = 1;
	this->active = 0;
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
	fmt.fmt.pix.width       = this->pos.width;
	fmt.fmt.pix.height      = this->pos.height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	fmt.fmt.pix.field       = V4L2_FIELD_ANY;
	if (-1 == xioctl(this->fd, VIDIOC_S_FMT, &fmt))
		errno_exit("VIDIOC_S_FMT");
	this->pos.width = fmt.fmt.pix.width;
	this->pos.height = fmt.fmt.pix.height;
	this->pos.x = 0.5 * (this->dingle_dots->drawing_rect.width - this->pos.width);
	this->pos.y = 0.5 * (this->dingle_dots->drawing_rect.height - this->pos.height);
    this->motion_jpeg_av_ctx = create_codec_context(this->pos.width, this->pos.height);
    this->motion_jpeg_avframe = av_frame_alloc();
    this->argb_bufsize = av_image_alloc(this->argb_data, this->argb_linesize,
        this->pos.width, this->pos.height, AV_PIX_FMT_ARGB, 1);
    this->motion_jpeg_to_rgba_format_ctx = sws_getContext(this->pos.width, this->pos.height, AV_PIX_FMT_YUVJ422P, 
        this->pos.width, this->pos.height, AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
	this->init_mmap();
}

void V4l2::uninit_device() {
	unsigned int i;
	for (i = 0; i < this->n_buffers; ++i)
		if (-1 == munmap(this->buffers[i].start, this->buffers[i].length))
			errno_exit("munmap");
	free(this->buffers);
    av_freep(&this->argb_data[0]);
}

int V4l2::deactivate()
{
	this->uninit();
	this->active = 0;
	return 0;
}

bool V4l2::is_done()
{
	return this->finished;
}

void *V4l2::thread(void *arg) {
	V4l2 *v = (V4l2 *)arg;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);
	int rc = pthread_setname_np(v->thread_id, "vw_v4l2");
	if (rc != 0) {
		errno = rc;
		perror("pthread_setname_np");
	}
	v->open_device();
	v->init_device();
	v->rbuf = jack_ringbuffer_create(5*4*v->pos.width*v->pos.height);
	memset(v->rbuf->buf, 0, v->rbuf->size);
	v->read_buf = (uint32_t *)(malloc(4 * v->pos.width * v->pos.height));
	v->allocated = 1;
    v->allocating = 0;
	v->start_capturing();
	pthread_mutex_lock(&v->lock);
	v->activate_spin(1.0);
	v->read_frames();
	pthread_mutex_unlock(&v->lock);
	v->stop_capturing();
	v->uninit_device();
	v->close_device();
	jack_ringbuffer_free(v->rbuf);
	free(v->read_buf);
	v->allocated = 0;
	return nullptr;
}

int V4l2::read_frames() {
	struct v4l2_buffer buf;
	struct timespec ts;
	unsigned char y0, y1, u, v;
	unsigned char r, g, b;
	unsigned char *ptr;
	int i, j, nij;
	int n;
	uint32_t pixel;
	int ret;
	while (!this->is_done()) {
		ret = poll(this->pfd, 1, 40);
		if (ret == 0 || !this->active) {
			continue;
		}
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
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
		assert(buf.index < this->n_buffers);

        decode_mjpeg(this->motion_jpeg_av_ctx, (unsigned char *)this->buffers[buf.index].start,
                     buf.bytesused, this->motion_jpeg_avframe); 

        sws_scale(this->motion_jpeg_to_rgba_format_ctx, (uint8_t const * const *)this->motion_jpeg_avframe->data,
            this->motion_jpeg_avframe->linesize, 0, this->pos.height, 
            this->argb_data, this->argb_linesize);
      
        if (this->mirrored) {
            AVFrame *frame = this->motion_jpeg_avframe;
            int width = frame->width;
            int height = frame->height;
            uint8_t* data = this->argb_data[0];
            int linesize = this->argb_linesize[0];
        
            // Iterate through each row
            for (int y = 0; y < height; ++y) {
                // Iterate through half of the columns in the row
                for (int x = 0; x < width / 2; ++x) {
                    // Calculate byte offsets for the two pixels to swap
                    uint8_t* pixel1 = data + y * linesize + x * 4;
                    uint8_t* pixel2 = data + y * linesize + (width - 1 - x) * 4;
        
                    // Swap the BGRA pixel data
                    uint32_t temp_pixel;
                    memcpy(&temp_pixel, pixel1, 4);
                    memcpy(pixel1, pixel2, 4);
                    memcpy(pixel2, &temp_pixel, 4);
                }
            }
        }
		clock_gettime(CLOCK_MONOTONIC, &ts);
		int space = this->argb_bufsize + sizeof(struct timespec);
		int buf_space = jack_ringbuffer_write_space(this->rbuf);
		if (buf_space >= space) {
			jack_ringbuffer_write(this->rbuf, (const char *)&ts,
								  sizeof(struct timespec));
			jack_ringbuffer_write(this->rbuf, (const char *)this->argb_data[0],
								  this->argb_bufsize);
		}
		if (-1 == xioctl(this->fd, VIDIOC_QBUF, &buf))
			errno_exit("VIDIOC_QBUF");
		new_frame_ready = True;
	}
	return 0;
}

bool V4l2::render(std::vector<cairo_t *> &contexts) {
	struct timespec ts;
	bool ret = false;
	if (this->active) {
		uint space = 4*this->pos.width*this->pos.height + sizeof(struct timespec);
		if (jack_ringbuffer_read_space(this->rbuf) >= space) {
			ret = true;
			jack_ringbuffer_read(this->rbuf, (char *)&ts, sizeof(struct timespec));
			jack_ringbuffer_read(this->rbuf, (char *)this->read_buf,  this->argb_bufsize);
		}
		cairo_surface_t *tsurf;
		tsurf = cairo_image_surface_create_for_data(
					(unsigned char *)this->read_buf, CAIRO_FORMAT_ARGB32,
					this->pos.width, this->pos.height, 4 * this->pos.width);
		render_surface(contexts, tsurf);
		cairo_surface_destroy(tsurf);
		new_frame_ready = jack_ringbuffer_read_space(this->rbuf) >= space;
	}
	return ret;
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

AVCodecContext* V4l2::create_codec_context(int width, int height) {
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if (!codec) {
        fprintf(stderr, "MJPEG decoder not found.\n");
        return NULL;
    }

    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        fprintf(stderr, "Failed to allocate codec context.\n");
        return NULL;
    }

    codec_context->width = width;
    codec_context->height = height;

    if (avcodec_open2(codec_context, codec, NULL) < 0) {
        fprintf(stderr, "Failed to open codec.\n");
        return NULL;
    }
    return codec_context;
}

void V4l2::decode_mjpeg(AVCodecContext *codec_context, unsigned char *input_buffer, int input_size, AVFrame *output_frame) {
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = input_buffer;
    pkt.size = input_size;

    int ret = avcodec_send_packet(codec_context, &pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending packet for decoding\n");
        return;
    }

    ret = avcodec_receive_frame(codec_context, output_frame);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        fprintf(stderr, "Error decoding frame\n");
    }
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

void V4l2::get_dimensions(std::string device, std::vector<std::pair<int, int>> &w_h) {
	struct v4l2_frmsizeenum frmsize;
	int fd;
	fd = open(device.c_str(), O_RDWR /* required */ | O_NONBLOCK, 0);
	if (-1 == fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n",
				device.c_str(), errno, strerror(errno));
	}
	frmsize.pixel_format = V4L2_PIX_FMT_MJPEG;
	frmsize.index = 1; /*Don't understand why 0 seems messed up. start at 1???*/
	while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0) {
		if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
			w_h.push_back(std::pair<int, int> (frmsize.discrete.width,
											   frmsize.discrete.height));
		}
		frmsize.index++;
	}
}
typedef std::vector<std::string> dev_vec;
typedef std::map<std::string, std::string> dev_map;

static bool is_v4l_dev(const char *name)
{
	const char *dev = "video";
	unsigned l = strlen(dev);
	if (!memcmp(name, dev, l)) {
		if (isdigit(name[l]))
			return true;
	}
	return false;
}

static int calc_node_val(const char *s)
{
	int n = 0;
	const char *dev = "video";
	s = strrchr(s, '/') + 1;
	unsigned l = strlen(dev);

	if (!memcmp(s, dev, l)) {
		n = 0 << 8;
		n += atol(s + l);
		return n;
	}

	return 0;
}

static bool sort_on_device_name(const std::string &s1, const std::string &s2)
{
	int n1 = calc_node_val(s1.c_str());
	int n2 = calc_node_val(s2.c_str());

	return n1 < n2;
}



void V4l2::list_devices(std::map<std::string, std::string> &cards) {
	DIR *dp;
	struct dirent *ep;
	dev_map links;
	struct v4l2_format fmt;
	struct v4l2_capability vcap;
	std::vector<std::string> files;
	dp = opendir("/dev");
	if (dp == NULL) {
		perror ("Couldn't open the directory");
		return;
	}
	while ((ep = readdir(dp))) {
		if (is_v4l_dev(ep->d_name)) {
			int fd;
			struct stat st;
			std::string name= std::string("/dev/") + ep->d_name;
			if (-1 == stat(name.c_str(), &st)) {
				continue;
			}
			if (!S_ISCHR(st.st_mode)) {
				continue;
			}
			fd = open(name.c_str(), O_RDWR /* required */ | O_NONBLOCK, 0);
			fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			fmt.fmt.pix.width       = 640;
			fmt.fmt.pix.height      = 360;
			fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
			fmt.fmt.pix.field       = V4L2_FIELD_ANY;
			if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
				continue;
			if (-1 == fd) {
				continue;
			} else {
				close(fd);
			}
			files.push_back(name);
		}
	}
	closedir(dp);

	/* Find device nodes which are links to other device nodes */
	for (dev_vec::iterator iter = files.begin(); iter != files.end(); ) {
		char link[64+1];
		int link_len;
		std::string target;

		link_len = readlink(iter->c_str(), link, 64);
		if (link_len < 0) {	/* Not a link or error */
			iter++;
			continue;
		}
		link[link_len] = '\0';

		/* Only remove from files list if target itself is in list */
		if (link[0] != '/')	/* Relative link */
			target = std::string("/dev/");
		target += link;
		if (find(files.begin(), files.end(), target) == files.end()) {
			iter++;
			continue;
		}

		/* Move the device node from files to links */
		if (links[target].empty())
			links[target] = *iter;
		else
			links[target] += ", " + *iter;
	}

	std::sort(files.begin(), files.end(), sort_on_device_name);
	for (dev_vec::iterator iter = files.begin();
		 iter != files.end(); ++iter) {
		int fd = open(iter->c_str(), O_RDWR);
		std::string bus_info;

		if (fd < 0)
			continue;
		int err = ioctl(fd, VIDIOC_QUERYCAP, &vcap);
		close(fd);
		if (err)
			continue;
		bus_info = (const char *)vcap.bus_info;
		if (cards[*iter].empty())
			cards[*iter] += *iter + " -- " +
					std::string((char *)vcap.card) + " (" + bus_info + ")";
		if (!(links[*iter].empty()))
			cards[*iter] += " <- " + links[*iter];
	}
}


