#include "x11.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>

X11::X11()
{
	display = NULL;
}

void X11::create(DingleDots *dd)
{
	XWindowAttributes DOSBoxWindowAttributes;
	display = XOpenDisplay(NULL);
	rootWindow = RootWindow(display, DefaultScreen(display));
	XGetWindowAttributes(display, rootWindow, &DOSBoxWindowAttributes);
	int width = DOSBoxWindowAttributes.width;
	int height = DOSBoxWindowAttributes.height;
	this->pos.width = width;
	this->pos.height = height;
	this->pos.x = 0;
	this->pos.y = 0;
	this->dingle_dots = dd;
	this->surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, this->pos.width,
									  this->pos.height);
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
				display, rootWindow, 0, 0, this->pos.width, this->pos.height, AllPlanes, ZPixmap);
	cairo_surface_flush(surf);
	unsigned char *data = cairo_image_surface_get_data(surf);
	red_mask = image->red_mask;
	green_mask = image->green_mask;
	blue_mask = image->blue_mask;
	for (int i = 0; i < this->pos.height; ++i) {
		for (int j = 0; j < this->pos.width; ++j) {
			colors.pixel = XGetPixel(image, j, i);
			// TODO(richard-to): Figure out why red and blue are swapped
			data[4*i*((int)this->pos.width) + 4*j] = (colors.pixel & blue_mask);
			data[4*i*((int)this->pos.width) + 4*j + 1] = (colors.pixel & green_mask)>>8;
			data[4*i*((int)this->pos.width) + 4*j + 2] = (colors.pixel & red_mask)>>16;
		}
	}
	render_surface(contexts, surf);
	XDestroyImage(image);
	return TRUE;
}
