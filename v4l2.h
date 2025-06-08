#if !defined (_V4L2_H)
#define _V4L2_H (1)

#include <poll.h>
#include <assert.h>
#include <map>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <jack/ringbuffer.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include "v4l2_wayland.h"
#include "vwdrawable.h"


#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define CLIPVALUE(v) ((v) < 255 ? (v) : 255)

#define NEVENTS 1
#define DD_V4L2_MAX_STR_LEN 256

struct dd_v4l2_buffer {
	void   *start;
	size_t  length;
};


class V4l2 : public vwDrawable {
public:
	V4l2();
	void init(DingleDots *dingle_dots, char *name, double w, double h, bool mirrored, uint64_t z);
	void uninit();
	int deactivate();
	bool is_done();
	bool is_new_frame_ready() { return new_frame_ready; };
	int read_frames();
	void stop_capturing();
	void start_capturing();
	void uninit_device();
	void init_device();
	void close_device();
	void open_device();
	void init_mmap();
    bool render(std::vector<cairo_t *> &contexts);

	static void get_dimensions(std::string device, std::vector<std::pair<int, int> > &w_h);
	static void list_devices(std::map<std::string, std::string> &files);
private:
	static void* thread(void *v);
	static int xioctl(int fh, int request, void *arg);

    AVCodecContext* create_codec_context(int width, int height);
    void decode_mjpeg(AVCodecContext *codec_context, unsigned char *input_buffer, int input_size, AVFrame *output_frame) ;	
   
	bool finished;
	bool new_frame_ready;
    bool mirrored;
public:
    /*libav used to decode motion jpeg.*/
    AVCodecContext *motion_jpeg_av_ctx;
    AVFrame *motion_jpeg_avframe;
    SwsContext *motion_jpeg_to_rgba_format_ctx;
    uint8_t *argb_data[4];
	int argb_linesize[4];
    int argb_bufsize;
	char dev_name[DD_V4L2_MAX_STR_LEN];
	int fd;
	struct dd_v4l2_buffer *buffers;
	unsigned int n_buffers;
	uint32_t *read_buf;
	struct pollfd pfd[1];
	pthread_t thread_id;
	pthread_mutex_t lock;
	jack_ringbuffer_t *rbuf;
};

#endif
