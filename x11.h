#ifndef X11_H
#define X11_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "vwdrawable.h"

class X11 : public vwDrawable
{
public:
	X11();
	void create(DingleDots *dd, int x, int y, int w, int h);
	void free();
	bool render(std::vector<cairo_t *> &contexts);
	void get_display_dimensions(int *w, int *h);
	rectangle_double xpos;

	private:
	Display *display;
	Window rootWindow;
	cairo_surface_t *surf;
};

#endif // X11_H
