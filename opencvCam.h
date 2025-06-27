#ifndef OPEN_CV_CAM_H
#define OPEN_CV_CAM_H

#include <opencv2/stitching.hpp>
#include <opencv2/opencv.hpp>
#include <pthread.h>
#include <gtk/gtk.h>
#include <jack/ringbuffer.h>

#include "v4l2_wayland.h"
#include "vwdrawable.h"


class OpenCVCam : public vwDrawable {
public:
	//OpenCVCam();
	void init(DingleDots *dingle_dots, int device_num, double w, double h, bool mirrored, uint64_t z);
	void uninit();
	bool render(std::vector<cairo_t *> &contexts);
    void do_thread();
    bool is_done();
	int deactivate();
    bool is_new_frame_ready() { return new_frame_ready; }
private:
    void start_capturing();
    void stop_capturing();
    int read_frames();
    void open_device();
    void close_device();
	static void *thread(void *arg);

    int device_num;
    uint32_t *read_buf; // Buffer to hold the read frame data
    pthread_t thread_id;
	pthread_mutex_t lock;
	pthread_cond_t data_ready;
	cairo_surface_t *surf;
    bool mirrored;
    cv::VideoCapture cap;
    bool new_frame_ready;
    bool finished;
	jack_ringbuffer_t *rbuf;

};

#endif // OPEN_CV_CAM_H
