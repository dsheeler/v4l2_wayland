#ifndef HEX_H
#define HEX_H

#include "v4l2_wayland.h"
#include "vwdrawable.h"
#include "vwcolor.h"

class Hex : public vwDrawable
{
public:
	Hex();
	~Hex() {}
	void create(double x, double y, double w, vwColor c,  DingleDots *dd);
	void free();
	bool render(std::vector<cairo_t *> &contexts);
};

#endif // HEX_H
