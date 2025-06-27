#include <utility>
#include <map>
#include <iostream>
#include <fstream>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include "dingle_dots.h"
#include "opencvCam.h"

static int check_new_frame_ready(void *data) {
    OpenCVCam *cam = (OpenCVCam *)data;
    if (cam->is_done()) {
        return false;
    } else if (cam->is_new_frame_ready()) {
        cam->dingle_dots->queue_draw();
    }
    return true;
}

void OpenCVCam::init(DingleDots *dd, int device_num, double width, double height, bool mirrored, uint64_t z) {
	this->allocating = 1;
    this->device_num = device_num;
    this->dingle_dots = dd;
	this->z = z;
	this->pos.x = 0;
	this->pos.y = 0;
	this->active = 0;
	this->finished = 0;
	this->selected = 0;
	this->mdown = 0;
	this->allocated = 0;
	this->new_frame_ready = false;
	this->pos.width = width;
	this->pos.height = height;
	this->mirrored = mirrored;
	this->hovered = false;
	g_timeout_add(10, check_new_frame_ready, this);
	pthread_create(&this->thread_id, nullptr, OpenCVCam::thread, this);

}


void OpenCVCam::uninit()
{
	this->finished = 1;
	this->active = 0;
    this->allocated = 0;
}



int OpenCVCam::deactivate()
{
	this->uninit();
	return 0;
}

bool OpenCVCam::is_done()
{
	return this->finished;
}

void *OpenCVCam::thread(void *arg) {
	OpenCVCam *cam = (OpenCVCam *)arg;
    cam->do_thread();
    pthread_exit(nullptr);
	return nullptr;
}

void OpenCVCam::open_device() {
    this->cap.open(this->device_num);
    if (!this->cap.isOpened()) {
        std::cerr << "Error: Could not open camera device " << this->device_num << std::endl;
        return;
    }
   this->cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G')); // Set MJPEG codec
    this->cap.set(cv::CAP_PROP_FRAME_WIDTH, this->pos.width);
    this->cap.set(cv::CAP_PROP_FRAME_HEIGHT, this->pos.height);
    //this->cap.set(cv::CAP_PROP_FPS, 60); // Set desired FPS


}

void OpenCVCam::close_device() {
    this->cap.release();
}

void OpenCVCam::do_thread() {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);
	int rc = pthread_setname_np(this->thread_id, "vw_opencv_cam");
	if (rc != 0) {
		errno = rc;
		perror("pthread_setname_np");
	}
	this->rbuf = jack_ringbuffer_create(5*4*this->pos.width*this->pos.height);
	memset(this->rbuf->buf, 0, this->rbuf->size);
	this->read_buf = (uint32_t *)(malloc(4 * this->pos.width * this->pos.height));
	this->allocated = 1;
    this->allocating = 0;
    this->open_device();
	this->start_capturing();
	pthread_mutex_lock(&this->lock);
	this->activate_spin(1.0);
	this->read_frames();
	pthread_mutex_unlock(&this->lock);
	this->stop_capturing();
	this->close_device();
	jack_ringbuffer_free(this->rbuf);
	free(this->read_buf);
	this->allocated = 0;
}

int OpenCVCam::read_frames() {
	
    cv::Mat frame, converted_frame;
    struct timespec ts;
  
    while(!this->finished && this->active) {
        if (!this->cap.read(frame)) {
            std::cerr << "Failed to read frame from camera." << std::endl;
            return -1;
      }
      if (frame.channels() == 3) {
          cv::cvtColor(frame, converted_frame, cv::COLOR_BGR2BGRA); // Convert BGR to BGRA
      } else if (frame.channels() == 4) {
          converted_frame = frame; // Already 4 channels
      } 
      if (this->mirrored) {
          cv::flip(converted_frame, converted_frame, 1); // Flip horizontally
      }    
      
        clock_gettime(CLOCK_MONOTONIC, &ts);
		int space = 4 * this->pos.width * this->pos.height + sizeof(struct timespec);
		int buf_space = jack_ringbuffer_write_space(this->rbuf);
		if (buf_space >= space) {
			jack_ringbuffer_write(this->rbuf, (const char *)&ts,
								  sizeof(struct timespec));
			jack_ringbuffer_write(this->rbuf, (const char *)converted_frame.data,
                                  4 * this->pos.width * this->pos.height);
		}
		new_frame_ready = true;
	}

	return 0;
}

bool OpenCVCam::render(std::vector<cairo_t *> &contexts) {
	struct timespec ts;
	bool ret = false;
	if (this->active) {
        uint buf_size = 4 * this->pos.width * this->pos.height;
		uint space = buf_size + sizeof(struct timespec);
		if (jack_ringbuffer_read_space(this->rbuf) >= space) {
			ret = true;
			jack_ringbuffer_read(this->rbuf, (char *)&ts, sizeof(struct timespec));
			jack_ringbuffer_read(this->rbuf, (char *)this->read_buf, buf_size);
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

void OpenCVCam::start_capturing() {
    // OpenCV handles the capturing automatically when reading frames
    // No specific start command is needed
}

void OpenCVCam::stop_capturing() {
    // OpenCV handles the stopping automatically when releasing the capture
    // No specific stop command is needed
}

