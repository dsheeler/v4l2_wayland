#ifndef TEXT_H
#define TEXT_H

#include "v4l2_wayland.h"
#include "vwdrawable.h"

class Text : public vwDrawable
{
public:
	Text();
	void init(char *text, char *font, uint font_size, DingleDots *dd);
	bool render(std::vector<cairo_t *> &contexts);
private:
	std::string *text;
	uint font_size;
	std::string *font;
	cairo_surface_t *surf;
	GdkPoint tpos;
};

#endif // TEXT_H
