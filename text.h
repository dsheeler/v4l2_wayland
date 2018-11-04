#ifndef TEXT_H
#define TEXT_H

#include "v4l2_wayland.h"
#include "vwdrawable.h"
#include "vwcolor.h"

class Text : public vwDrawable
{
public:
	Text();
	void init(char *text, char *font, DingleDots *dd);
	bool render(std::vector<cairo_t *> &contexts);
	void set_color_red(double r) { set_color(R, r); }
	void set_color_green(double g) { set_color(G, g); }
	void set_color_blue(double b) { set_color(B, b); }
	void set_color_alpha(double a) { set_color(A, a); }
	void set_color_hue(double h) { set_color(H, h); }
	void set_color_saturation(double s) { set_color(S, s); }
	void set_color_value(double v) { set_color(V, v); }
	void set_color_rgba(double r, double g, double v, double a);

	void set_color_hsva(double h, double s, double v, double a);
	private:
	void set_color(color_prop p, double v);
	std::string *text;
	std::string *font;
	cairo_surface_t *surf;
	GdkPoint tpos;
	vwColor color;
};

#endif // TEXT_H
