#include "snapshot_shape.h"
#include "dingle_dots.h"

SnapshotShape::SnapshotShape()
{

}

void SnapshotShape::init(string &label, double x, double y, double r, color c,
						 void *dd)
{
	this->clear_state();
	this->pos.x = x;
	this->pos.y = y;
	this->dd = (DingleDots *)dd;
	this->z = this->dd->next_z++;
	if (this->label) delete this->label;
	this->label = new string(label);
	this->r = r;
	this->color_normal = color_copy(&c);
	this->color_on = color_lighten(&c, 0.95);
	this->shutdown_time = 5.0;
}

int SnapshotShape::set_on() {
	this->on = 1;
	return 1;
}

int SnapshotShape::set_off() {
	this->on = 0;
	dd->do_snapshot = 1;
	return 1;
}

bool SnapshotShape::render(std::vector<cairo_t *> &contexts) {
	color *c;
	c = &this->color_normal;
	for (std::vector<cairo_t *>::iterator it = contexts.begin(); it != contexts.end(); ++it) {
		cairo_t *cr = *it;
		cairo_save(cr);
		cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a);
		cairo_translate(cr, this->pos.x, this->pos.y);
		cairo_arc(cr, 0, 0, this->r*0.975, 0, 2. * M_PI);
		cairo_fill(cr);
		cairo_arc(cr, 0, 0, this->r, 0, 2*M_PI);
		cairo_set_source_rgba(cr, 0.5*c->r, 0.5*c->g, 0.5*c->b, c->a);
		cairo_set_line_width(cr, 0.05 * this->r);
		cairo_stroke(cr);
		if (this->on) {
			double diff_sec = get_secs_since_last_on();
			double radius = this->r * 1.025 * (diff_sec / this->shutdown_time);
			cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
			cairo_arc(cr, 0, 0, radius, 0, 2*M_PI);
			cairo_fill(cr);
		}
		if (this->hovered) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
			cairo_arc(cr, 0, 0, this->r, 0, 2*M_PI);
			cairo_set_line_width(cr, 0.05 * this->r);
			cairo_stroke(cr);
			/*cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
		cairo_arc(cr, 0, 0, this->r * 1.025, 0, 2 * M_PI);
	cairo_fill(cr);*/
		}
		if (this->selected) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
			cairo_arc(cr, 0, 0, this->r*1.025, 0, 2*M_PI);
			cairo_fill(cr);
		}
		cairo_restore(cr);
		this->render_label(cr);
	}
	return true;
}

void SnapshotShape::set_motion_state(uint8_t state) {
	if (state && this->motion_state == 0) {
		this->motion_state = 1;
		this->motion_state_to_off = 0;
		clock_gettime(CLOCK_MONOTONIC, &this->motion_ts);
	} else {
		if (this->motion_state) {
			double diff_sec = get_secs_since_last_on();
			if (diff_sec >= shutdown_time) {
				this->motion_state = 0;
				this->motion_state_to_off = 0;
			} else {
				this->motion_state_to_off = 1;
			}
		}
	}
}
