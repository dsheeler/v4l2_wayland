#include <math.h>

#include "draggable.h"


double Drawable::get_opacity() const
{
	return opacity;
}

void Drawable::set_opacity(double value)
{
	opacity = value > 1.0 ? 1.0 : (value < 0.0 ? 0.0 : value);
}

double Drawable::get_scale() const
{
	return scale;
}

void Drawable::set_scale(double value)
{
	scale = value < 0 ? scale : value;
}

Drawable::Drawable() {
	pos.x = 0.0;
	pos.y = 0.0;
	z = 0;
	this->rotation_radians = 0.0;
	mdown = 0;
	opacity = 1.0;
	scale = 1.0;
}

Drawable::Drawable(double x, double y, int64_t z, double opacity, double scale) {
	pos.x = x;
	pos.y = y;
	this->z = z;
	this->rotation_radians = 0.0;
	this->opacity = opacity;
	mdown = 0;
	this->scale = scale;
}
bool Drawable::render(std::vector<cairo_t *> &contexts) {
	return FALSE;
}

bool Drawable::render_surface(std::vector<cairo_t *> &contexts, cairo_surface_t *surf) {
	for (std::vector<cairo_t *>::iterator it = contexts.begin(); it != contexts.end(); ++it) {
		cairo_t *cr = *it;
		cairo_save(cr);
		cairo_translate(cr, this->pos.x, this->pos.y);
		cairo_translate(cr, 0.5 * this->pos.width, 0.5 * this->pos.height);
		cairo_scale(cr, this->scale, this->scale);
		cairo_rotate(cr, this->get_rotation());
		cairo_translate(cr, -0.5 * this->pos.width, -0.5 * this->pos.height);
		cairo_set_source_surface(cr, surf, 0.0, 0.0);
		double o = this->get_opacity();
		cairo_paint_with_alpha(cr, o);
		if (this->hovered) {
			render_hovered(cr);
		} else {
			render_shadow(cr);
		}
		cairo_restore(cr);
	}
	return TRUE;
}

void Drawable::rotate(double angle)
{
	this->rotation_radians += angle;
}

void Drawable::set_rotation(double angle)
{
	this->rotation_radians = angle;
}

void Drawable::set_mdown(double x, double y, int64_t z) {
	mdown = 1;
	this->z = z;
	mdown_pos.x = x;
	mdown_pos.y = y;
	down_pos.x = pos.x;
	down_pos.y = pos.y;
}

void Drawable::drag(double mouse_x, double mouse_y) {
	if (mdown) {
		pos.x = mouse_x - mdown_pos.x + down_pos.x;
		pos.y = mouse_y - mdown_pos.y + down_pos.y;
	}
}

int Drawable::in(double x_in, double y_in) {
	double xc = x_in - (this->pos.x + 0.5 * this->pos.width);
	double yc = y_in - (this->pos.y + 0.5 * this->pos.height);
	double x = fabs((1./this->scale) * (cos(this->rotation_radians) * xc + sin(this->rotation_radians) * yc));
	double y = fabs((1./this->scale) * (- sin(this->rotation_radians) * xc + cos(this->rotation_radians) * yc));
	if ((x <= 0.5 * this->pos.width) && (y <= 0.5 * this->pos.height)) {
		return 1;
	} else {
		return 0;
	}
}

void Drawable::render_halo(cairo_t *cr, color c, double len) {
	cairo_pattern_t *pat;
	cairo_save(cr);
	pat = cairo_pattern_create_linear(0.0, -len, 0.0, 0.0);
	cairo_pattern_add_color_stop_rgba(pat,0.0, c.r, c.g, c.b, 0);
	cairo_pattern_add_color_stop_rgba(pat, 1, c.r, c.g, c.b, c.a);
	cairo_set_source(cr, pat);
	cairo_rectangle(cr, 0., -len, this->pos.width, len);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
	pat = cairo_pattern_create_linear(0.0, this->pos.height, 0.0, this->pos.height + len);
	cairo_pattern_add_color_stop_rgba(pat,1, c.r, c.g, c.b, 0);
	cairo_pattern_add_color_stop_rgba(pat, 0, c.r, c.g, c.b, c.a);
	cairo_set_source(cr, pat);
	cairo_rectangle(cr, 0., this->pos.height, this->pos.width, len);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
	pat = cairo_pattern_create_linear(-len, 0.0, 0.0, 0.0);
	cairo_pattern_add_color_stop_rgba(pat,0.0, c.r, c.g, c.b, 0);
	cairo_pattern_add_color_stop_rgba(pat, 1, c.r, c.g, c.b, c.a);
	cairo_set_source(cr, pat);
	cairo_rectangle(cr, -len, 0, len, this->pos.height);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
	pat = cairo_pattern_create_linear(this->pos.width, 0.0, this->pos.width + len, 0.0);
	cairo_pattern_add_color_stop_rgba(pat, 0, c.r, c.g, c.b, c.a);
	cairo_pattern_add_color_stop_rgba(pat, 1, c.r, c.g, c.b, 0);
	cairo_set_source(cr, pat);
	cairo_rectangle(cr, this->pos.width, 0.0, len, this->pos.height);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
	cairo_save(cr);
	pat = cairo_pattern_create_radial(0.0, 0.0, 0, 0.0, 0.0, len);
	cairo_pattern_add_color_stop_rgba(pat, 0, c.r, c.g, c.b, c.a);
	cairo_pattern_add_color_stop_rgba(pat, 1, c.r, c.g, c.b, 0);
	cairo_set_source(cr, pat);
	cairo_rectangle(cr, -len, -len, len, len);
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_save(cr);
	cairo_translate(cr, this->pos.width, 0);
	cairo_rotate(cr, M_PI_2);
	cairo_set_source(cr, pat);
	cairo_rectangle(cr, -len, -len, len, len);
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_save(cr);
	cairo_translate(cr, this->pos.width, this->pos.height);
	cairo_rotate(cr, M_PI);
	cairo_set_source(cr, pat);
	cairo_rectangle(cr, -len, -len, len, len);
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_translate(cr, 0, this->pos.height);
	cairo_rotate(cr, 3 * M_PI_2);
	cairo_set_source(cr, pat);
	cairo_rectangle(cr, -len, -len, len, len);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
	cairo_restore(cr);
}

void Drawable::render_hovered(cairo_t *cr) {
	color c;
	c.r = 0.25;
	c.g = 0;
	c.b = 1;
	c.a = 0.5;
	cairo_rectangle(cr, 0.0, 0.0, this->pos.width, this->pos.height);
	cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
	cairo_fill(cr);
	render_halo(cr, c, 5.0);
}

void Drawable::render_shadow(cairo_t *cr) {
	color c;
	c.r = 0;
	c.g = 0;
	c.b = 0;
	c.a = 0.5;
	render_halo(cr, c, 5);
}
