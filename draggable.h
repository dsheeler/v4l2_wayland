#if !defined (_DRAGGABLE_H)
#define _DRAGGABLE_H (1)

#include "gtk/gtk.h"
#include <stdint.h>

class Draggable {
	private:
		void render_label(cairo_t *cr);
	public:
		uint64_t z;
		int mdown;
	//protected:
		GdkRectangle pos;
		GdkRectangle mdown_pos;
		GdkRectangle down_pos;
	public:
		Draggable();
		Draggable(double x, double y, uint64_t z);
		void set_mdown(double x, double y, uint64_t z);
		void drag(double mouse_x, double mouse_y);
};

#endif
