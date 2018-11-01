#include <math.h>
#include <functional>

#include "vwdrawable.h"
#include "easer.h"
#include "dingle_dots.h"

double vwDrawable::get_opacity() const
{
	return opacity;
}

void vwDrawable::set_opacity(double value)
{
	opacity = value > 1.0 ? 1.0 : (value < 0.0 ? 0.0 : value);
	gtk_widget_queue_draw(this->dingle_dots->drawing_area);
}

double vwDrawable::get_scale() const
{
	return scale;
}

void vwDrawable::set_scale(double value)
{
	scale = value < 0 ? scale : value;
	gtk_widget_queue_draw(this->dingle_dots->drawing_area);
}

int vwDrawable::fade_in(double duration) {
	this->opacity = 0.0;
	Easer *e = new Easer();
	e->initialize(this, EASER_LINEAR, std::bind(&vwDrawable::set_opacity, this, std::placeholders::_1), 0, 1.0, duration);
	e->start();
	return 0;
}

int vwDrawable::scale_to_fit(double duration) {
	this->scale = min(this->dingle_dots->drawing_rect.width / this->pos.width,
					  this->dingle_dots->drawing_rect.height / this->pos.height);
	Easer *e = new Easer();
	e->initialize(this, EASER_BACK_EASE_OUT, std::bind(&vwDrawable::set_scale, this, std::placeholders::_1), 0, this->scale, duration);
	e->start();
	return 0;
}

int vwDrawable::activate_spin() {
	double duration = 2;
	if (!this->active) {
		this->easers.clear();
		fade_in(duration);
		this->scale = min(this->dingle_dots->drawing_rect.width / this->pos.width,
						  this->dingle_dots->drawing_rect.height / this->pos.height);
		Easer *er2 = new Easer();
		er2->initialize(this, EASER_SINE_EASE_OUT, std::bind(&vwDrawable::set_rotation, this, std::placeholders::_1), -3*2*M_PI, 0, 2*duration);
		er2->start();
		Easer *er3 = new Easer();
		er3->initialize(this, EASER_SINE_EASE_OUT, std::bind(&vwDrawable::set_scale, this, std::placeholders::_1), 0, 1, duration);
		er3->start();
		gtk_widget_queue_draw(this->dingle_dots->drawing_area);
		this->active = 1;

	}
	return 0;
}
int vwDrawable::activate() {
	if (!this->active) {
		this->set_scale(1.0);
		this->easers.clear();
		this->fade_in(1.0);
		this->active = 1;
	}
	return 0;
}


void vwDrawable::deactivate_action()
{
	if (active) {
		this->active = 0;
	}
}

int vwDrawable::deactivate() {
	double duration = 0.4;
	Easer *e = new Easer();
	Easer *e2 = new Easer();
	e->initialize(this,EASER_CIRCULAR_EASE_IN_OUT, std::bind(&vwDrawable::set_scale,
															   this, std::placeholders::_1),
				  this->scale, 3 * this->scale, 0.75 * duration);
	e2->initialize(this, EASER_CIRCULAR_EASE_IN_OUT, std::bind(&vwDrawable::set_scale,
																 this, std::placeholders::_1),
				   3 * this->scale, 0, 0.25 * duration);
	e2->add_finish_action(std::bind(&vwDrawable::deactivate_action, this));
	e->add_finish_easer(e2);
	e->start();
	return 0;
}


DingleDots *vwDrawable::get_dingle_dots() const
{
	return dingle_dots;
}

void vwDrawable::set_dingle_dots(DingleDots *value)
{
	dingle_dots = value;
}

vwDrawable::vwDrawable() {
	pos.x = 0.0;
	pos.y = 0.0;
	z = 0;
	this->rotation_radians = 0.0;
	mdown = 0;
	opacity = 1.0;
	scale = 1.0;
}

vwDrawable::vwDrawable(double x, double y, int64_t z, double opacity, double scale) {
	pos.x = x;
	pos.y = y;
	this->z = z;
	this->rotation_radians = 0.0;
	this->opacity = opacity;
	mdown = 0;
	this->scale = scale;
}



bool vwDrawable::render(std::vector<cairo_t *> &) {
	return FALSE;
}

bool vwDrawable::render_surface(std::vector<cairo_t *> &contexts, cairo_surface_t *surf) {
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

void vwDrawable::rotate(double angle)
{
	this->rotation_radians += angle;
	gtk_widget_queue_draw(this->dingle_dots->drawing_area);
}

void vwDrawable::set_rotation(double angle)
{
	this->rotation_radians = angle;
	gtk_widget_queue_draw(this->dingle_dots->drawing_area);
}

void vwDrawable::set_mdown(double x, double y, int64_t z) {
	mdown = 1;
	this->z = z;
	mdown_pos.x = x;
	mdown_pos.y = y;
	down_pos.x = pos.x;
	down_pos.y = pos.y;
}

void vwDrawable::drag(double mouse_x, double mouse_y) {
	int detent_len = 20;
	double res;
	if (mdown) {
		pos.x = mouse_x - mdown_pos.x + down_pos.x;
		pos.y = mouse_y - mdown_pos.y + down_pos.y;
		res = (pos.x + 0.5 * pos.width - 0.5 * (this->scale) * pos.width);
		if (-detent_len < res && res < detent_len) pos.x = 0.5 * this->scale * pos.width - 0.5 * pos.width;
		res = (pos.x + 0.5 * pos.width + 0.5 * (this->scale) * pos.width) - dingle_dots->drawing_rect.width;
		if (-detent_len < res && res < detent_len) pos.x = dingle_dots->drawing_rect.width - 0.5 * scale * pos.width - 0.5 * pos.width;
		res = (pos.y + 0.5 * pos.height - 0.5 * (this->scale) * pos.height);
		if (-detent_len < res && res < detent_len) pos.y = 0.5 * this->scale * pos.height - 0.5 * pos.height;
		res = (pos.y + 0.5 * pos.height + 0.5 * (this->scale) * pos.height) - dingle_dots->drawing_rect.height;
		if (-detent_len < res && res < detent_len) pos.y = dingle_dots->drawing_rect.height - 0.5 * scale * pos.height - 0.5 * pos.height;
		res = (pos.x + 0.5 * pos.width) - 0.5 * dingle_dots->drawing_rect.width;
		if (-detent_len < res && res < detent_len) pos.x = 0.5 * dingle_dots->drawing_rect.width -0.5 * pos.width;
		res = (pos.y + 0.5 * pos.height) - 0.5 * dingle_dots->drawing_rect.height;
		if (-detent_len < res && res < detent_len) pos.y = 0.5 * dingle_dots->drawing_rect.height -0.5 * pos.height;
	}
}

int vwDrawable::in(double x_in, double y_in) {
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

void vwDrawable::render_halo(cairo_t *cr, color c, double in_length) {
	cairo_pattern_t *pat;
	double len = in_length / this->scale;
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

void vwDrawable::render_hovered(cairo_t *cr) {
	color c;
	c.r = 0.1;
	c.g = 0.5;
	c.b = 0.85;
	c.a = 0.25;
	cairo_rectangle(cr, 0.0, 0.0, this->pos.width, this->pos.height);
	cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
	cairo_fill(cr);
	render_halo(cr, c, 5.0);
}

void vwDrawable::render_shadow(cairo_t *cr) {
	color c;
	c.r = 0;
	c.g = 0;
	c.b = 0;
	c.a = 0.75;
	render_halo(cr, c, 10);
}
