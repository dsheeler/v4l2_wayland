#include "x11.h"
#include "dingle_dots.h"
#include "x11.h"
#include "x11.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>

#define WIN_STRING_DIV "\r\n"

X11::X11()
{
	XInitThreads();
	display = NULL;
}

std::list<Window> X11::get_top_level_windows()
{
	std::list<Window> res;
	Display *disp = XOpenDisplay(NULL);

	Atom netClList = XInternAtom(disp, "_NET_CLIENT_LIST", true);
	Atom actualType;
	int format;
	unsigned long num, bytes;
	Window* data = 0;


	for (int i = 0; i < ScreenCount(disp); ++i) {
		Window rootWin = RootWindow(disp, i);

		int status = XGetWindowProperty(
						 disp,
						 rootWin,
						 netClList,
						 0L,
						 ~0L,
						 false,
						 AnyPropertyType,
						 &actualType,
						 &format,
						 &num,
						 &bytes,
						 (uint8_t**)&data);

		if (status != Success) {
			continue;
		}

		for (unsigned long i = 0; i < num; ++i)
			res.push_back(data[i]);

		XFree(data);
	}

	return res;
}

int getRootWindowScreen(Window root)
{
	XWindowAttributes attr;
	Display *disp = XOpenDisplay(NULL);

	if (!XGetWindowAttributes(disp, root, &attr))
		return DefaultScreen(disp);

	return XScreenNumberOfScreen(attr.screen);
}

std::string getWindowAtom(Window win, const char *atom)
{
	Display *disp = XOpenDisplay(NULL);
	Atom netWmName = XInternAtom(disp, atom, false);
	int n;
	char **list = 0;
	XTextProperty tp;
	std::string res = "unknown";

	XGetTextProperty(disp, win, &tp, netWmName);

	if (!tp.nitems)
		XGetWMName(disp, win, &tp);

	if (!tp.nitems)
		return "error";

	if (tp.encoding == XA_STRING) {
		res = (char*)tp.value;
	} else {
		int ret = XmbTextPropertyToTextList(disp, &tp, &list,
				&n);

		if (ret >= Success && n > 0 && *list) {
			res = *list;
			XFreeStringList(list);
		}
	}

//	char *conv = nullptr;
//	if (os_mbs_to_utf8_ptr(res.c_str(), 0, &conv))
//		res = conv;
//	bfree(conv);

	XFree(tp.value);

	return res;
}

std::string getWindowClass(Window win)
	{
		return getWindowAtom(win, "WM_CLASS");
	}

std::string X11::get_window_name(Window win)
	{
		return getWindowAtom(win, "_NET_WM_NAME");
	}

Window X11::get_window_from_string(std::string wstr)
{
	Display *disp = XOpenDisplay(NULL);

	if (wstr == "") {
		return X11::get_top_level_windows().front();
	}
	for (Window cwin: X11::get_top_level_windows()) {
		std::string cwinname = X11::get_window_name(cwin);
		std::string ccls = getWindowClass(cwin);

		if (wstr == cwinname)
			return cwin;
	}
	return -1;
}

void X11::get_display_dimensions(int *w, int*h) {
	XWindowAttributes DOSBoxWindowAttributes;
	Display *display = XOpenDisplay(NULL);
	Window rootWindow = RootWindow(display, DefaultScreen(display));
	XGetWindowAttributes(display, rootWindow, &DOSBoxWindowAttributes);
	*w = DOSBoxWindowAttributes.width;
	*h = DOSBoxWindowAttributes.height;
}

void *X11::thread(void *arg) {
	X11 *x = (X11 *)arg;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);
	int rc = pthread_setname_np(x->thread_id, "vw_x11");
	if (rc != 0) {
		errno = rc;
		perror("pthread_setname_np");
	}
	x->event_loop();
	return nullptr;
}

void X11::event_loop() {
	this->surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
											this->xpos.width,
											this->xpos.height);
	this->allocated = 1;
	Display *new_disp = XOpenDisplay(nullptr);
	XSelectInput(new_disp, this->window, StructureNotifyMask);
	while (!this->done) {
		XEvent e;
		XNextEvent (new_disp, & e);
		pthread_mutex_lock(&this->lock);
		if (e.type == ConfigureNotify) {
			this->allocated	= 0;
			cairo_surface_destroy(this->surf);
			this->pos.width = this->xpos.width = e.xconfigure.width;
			this->pos.height = this->xpos.height = e.xconfigure.height;
			this->xpos.x = e.xconfigure.x;
			this->xpos.y = e.xconfigure.y;
			this->surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
													this->xpos.width,
													this->xpos.height);
			this->allocated = 1;
		}
		pthread_mutex_unlock(&this->lock);
	}
}

void X11::init(DingleDots *dd, int x, int y, int w, int h) {
	this->display = XOpenDisplay(NULL);
	this->rootWindow = RootWindow(display, DefaultScreen(display));
	this->xpos.width = w;
	this->xpos.height = h;
	this->xpos.x = x;
	this->xpos.y = y;
	this->pos.width = w;
	this->pos.height = h;
	this->pos.x = 0;
	this->pos.y = 0;
	this->done = false;
	this->dingle_dots = dd;
	this->surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, this->xpos.width,
									  this->xpos.height);
	this->allocated = 1;
	//pthread_create(&this->thread_id, nullptr, X11::thread, this);

}

void X11::init_window(DingleDots *dd, Window win) {
	int w, h;
	XWindowAttributes win_att;
	this->window = win;
	this->display = XOpenDisplay(NULL);
	this->rootWindow = RootWindow(display, DefaultScreen(display));
	XGetWindowAttributes(this->display, this->window, &win_att);
	w = win_att.width;
	h = win_att.height;
	this->xpos.width = w;
	this->xpos.height = h;
	this->xpos.x = win_att.x;
	this->xpos.y = win_att.y;
	this->pos.width = w;
	this->pos.height = h;
	this->pos.x = 0;
	this->pos.y = 0;
	this->done = false;
	this->dingle_dots = dd;
	pthread_create(&this->thread_id, nullptr, X11::thread, this);
}

void X11::free()
{
	this->allocated = 0;
	this->active = 0;
	cairo_surface_destroy(this->surf);
	pthread_cancel(this->thread_id);
}

bool X11::render(std::vector<cairo_t *> &contexts) {
	if (!this->active || !this->allocated) return TRUE;
	XColor colors;
	XImage *image;
	unsigned long red_mask;
	unsigned long green_mask;
	unsigned long blue_mask;
	if (pthread_mutex_trylock(&this->lock) == 0) {
		if (this->dingle_dots->use_window_x11) {
			image = XGetImage(
						display, this->window, 0, 0, this->xpos.width,
						this->xpos.height, AllPlanes, ZPixmap);
		} else {
			image = XGetImage(
						display, rootWindow, this->xpos.x, this->xpos.y, this->xpos.width,
						this->xpos.height, AllPlanes, ZPixmap);
		}
		if (image) {
			cairo_surface_flush(surf);
			unsigned char *data = cairo_image_surface_get_data(surf);
			red_mask = image->red_mask;
			green_mask = image->green_mask;
			blue_mask = image->blue_mask;
			for (int i = 0; i < this->xpos.height; ++i) {
				for (int j = 0; j < this->xpos.width; ++j) {
					colors.pixel = XGetPixel(image, j, i);
					data[4*i*((int)this->xpos.width) + 4*j] = (colors.pixel & blue_mask);
					data[4*i*((int)this->xpos.width) + 4*j + 1] = (colors.pixel & green_mask)>>8;
					data[4*i*((int)this->xpos.width) + 4*j + 2] = (colors.pixel & red_mask)>>16;
				}
			}
			XDestroyImage(image);
		}
		render_surface(contexts, surf);
		pthread_mutex_unlock(&this->lock);
	}
	return TRUE;
}

void X11::deactivate_action()
{
	this->free();
}
