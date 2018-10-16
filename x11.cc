#include "x11.h"
#include "dingle_dots.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>

X11::X11()
{
	display = NULL;
}

void X11::get_display_dimensions(int *w, int*h) {
	XWindowAttributes DOSBoxWindowAttributes;
	Display *display = XOpenDisplay(NULL);
	Window rootWindow = RootWindow(display, DefaultScreen(display));
	XGetWindowAttributes(display, rootWindow, &DOSBoxWindowAttributes);
	*w = DOSBoxWindowAttributes.width;
	*h = DOSBoxWindowAttributes.height;
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
	this->dingle_dots = dd;
	this->surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, this->xpos.width,
									  this->xpos.height);
	this->allocated = 1;
}

Window X11::get_window_under_click() {
	Window win;
	int ret;
	XEvent e;
	Display *display = XOpenDisplay(NULL);
	Window rootWindow = RootWindow(display, DefaultScreen(display));
	ret = XGrabPointer(display, rootWindow, 0, ButtonPressMask,
					   GrabModeAsync, GrabModeAsync, rootWindow,
					   None, CurrentTime);
	XNextEvent(display, &e);
	XUngrabPointer(display, CurrentTime);
	if (e.type == ButtonPress) {
		int w, h;
		XWindowAttributes win_att;
		win = e.xbutton.subwindow;
		XGetWindowAttributes(display, win, &win_att);
		w = win_att.width;
		h = win_att.height;
	}
	return win;
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
	this->xpos.x = 0;
	this->xpos.y = 0;
	this->pos.width = w;
	this->pos.height = h;
	this->pos.x = 0;
	this->pos.y = 0;
	this->dingle_dots = dd;
	this->surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, this->xpos.width,
									  this->xpos.height);
	XSelectInput(this->display, this->window, ConfigureNotify);
	this->allocated = 1;
}

void X11::free()
{
	this->allocated = 0;
	this->active = 0;
	cairo_surface_destroy(this->surf);
}

bool X11::render(std::vector<cairo_t *> &contexts) {
	if (!this->active) return TRUE;
	XColor colors;
	XImage *image;
	XEvent e;
	while (XPending(display)) {
		XNextEvent(this->display, &e);
	}
	unsigned long red_mask;
	unsigned long green_mask;
	unsigned long blue_mask;
	if (this->dingle_dots->use_window_x11) {
		image = XGetImage(
					display, this->window, 0, 0, this->xpos.width,
					this->xpos.height, AllPlanes, ZPixmap);
	} else {
		image = XGetImage(
					display, rootWindow, this->xpos.x, this->xpos.y, this->xpos.width,
					this->xpos.height, AllPlanes, ZPixmap);
	}
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
	render_surface(contexts, surf);
	return TRUE;
}
