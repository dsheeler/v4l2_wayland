#ifndef TEXT_H
#define TEXT_H

#include "v4l2_wayland.h"
#include "vwdrawable.h"
#include "vwcolor.h"

class Text : public vwDrawable
{
public:
	Text();
	~Text() {}
	void create(const char *text, const char *font, double x, double y, vwColor color, DingleDots *dd);
	void free();
	bool render(std::vector<cairo_t *> &contexts);

private:
	std::string text;
	std::string font;
};

#endif // TEXT_H
