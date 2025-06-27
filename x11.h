#ifndef X11_H
#define X11_H

#include "vwdrawable.h"
#include <opencv2/opencv.hpp>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>

class X11 : public vwDrawable
{
public:
	X11();
	void init(DingleDots *dd, int x, int y, int w, int h);
	void init_window(DingleDots *dd, Window win);
	void uninit();
	bool render(std::vector<cairo_t *> &contexts);
	void deactivate_action();
	static void get_display_dimensions(int *w, int *h);
	static std::list<Window> get_top_level_windows();
	GdkRectangle xpos;
	pthread_t thread_id;
	pthread_mutex_t lock;
	pthread_cond_t data_ready;
	static Window get_window_under_click();
	static void *thread(void *arg);
	static std::string get_window_name(Window win);
	static Window get_window_from_string(std::string wstr);
private:
	Display *display;
	Window rootWindow;
	Window window;
	XImage *shm_image;
	cairo_surface_t *surf;
	bool done;
	bool using_window;
	void event_loop();
};

#endif // X11_H
