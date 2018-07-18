#include "x11.h"
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

void X11::create(DingleDots *dd, int x, int y, int w, int h)
{
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

	unsigned long red_mask;
	unsigned long green_mask;
	unsigned long blue_mask;

	image = XGetImage(
				display, rootWindow, this->xpos.x, this->xpos.y, this->xpos.width,
				this->xpos.height, AllPlanes, ZPixmap);
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
