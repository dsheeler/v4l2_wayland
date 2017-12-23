#if !defined (_DRAGGABLE_H)
#define _DRAGGABLE_H (1)

#include "gtk/gtk.h"
#include <vector>
#include <stdint.h>

class Draggable {
private:
	void render_label(cairo_t *cr);
public:
	uint64_t z;
	int mdown;
	//protected:
	double rotation_radians;
	GdkRectangle pos;
	GdkPoint mdown_pos;
	GdkPoint down_pos;
public:
	Draggable();
	virtual ~Draggable() {}
	Draggable(double x, double y, uint64_t z);
	void set_mdown(double x, double y, uint64_t z);
	void drag(double mouse_x, double mouse_y);
	virtual int in(double x_in, double y_in);
	friend bool operator<(const Draggable& l, const Draggable& r) {
		return l.z < r.z;
	}
	virtual bool render(std::vector<cairo_t *> &contexts);
	void rotate(double angle);
	void set_rotation(double angle);
	double get_rotation() { return this->rotation_radians; }
};

#endif
