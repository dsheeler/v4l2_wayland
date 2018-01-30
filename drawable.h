#if !defined (_DRAWABLE_H)
#define _DRAWABLE_H (1)

#include "gtk/gtk.h"
#include <vector>
#include <stdint.h>
#include "v4l2_wayland.h"
#include "easer.h"

struct rectangle_double {
	double x;
	double y;
	double width;
	double height;
};

//class Easer;
class Drawable {
private:
	void render_label(cairo_t *cr);
public:
	int64_t z;
	int mdown;
	//protected:
	double scale;
	double rotation_radians;
	double opacity;
	rectangle_double pos;
	GdkPoint mdown_pos;
	GdkPoint down_pos;
	uint8_t hovered;
	uint8_t allocated;
	uint8_t active;
	bool selected;
	GdkPoint selected_pos;
	std::vector<Easer *> easers;
	DingleDots *dingle_dots;
public:
	Drawable();
	virtual ~Drawable() {}
	Drawable(double x, double y, int64_t z, double opacity, double scale);
	virtual int deactivate();
	virtual int activate();
	void update_easers();
	void set_mdown(double x, double y, int64_t z);
	void drag(double mouse_x, double mouse_y);
	virtual int in(double x_in, double y_in);
	friend bool operator<(const Drawable& l, const Drawable& r) {
		return l.z < r.z;
	}
	virtual bool render(std::vector<cairo_t *> &);
	void rotate(double angle);
	void set_rotation(double angle);
	double get_rotation() { return this->rotation_radians; }
	double get_opacity() const;
	void set_opacity(double value);
	virtual void render_hovered(cairo_t *cr);
	void render_halo(cairo_t *cr, color c, double len);
	virtual void render_shadow(cairo_t *cr);
	bool render_surface(std::vector<cairo_t *> &contexts, cairo_surface_t *surf);
	double get_scale() const;
	void set_scale(double value);
	DingleDots *get_dingle_dots() const;
	void set_dingle_dots(DingleDots *value);

	int activate_spin_and_scale_to_fit();
	int scale_to_fit(double duration);
protected:
	virtual void deactivate_action();
};

#endif
