#include "snapshot_shape.h"
#include "dingle_dots.h"

#define SHUTDOWN_SECS 5

SnapshotShape::SnapshotShape()
{
	active = 0;
}

void SnapshotShape::init(const char *label, double x, double y, double r, color c,
						 void *dd)
{
	this->clear_state();
	this->pos.x = x;
	this->pos.y = y;
	this->dingle_dots = (DingleDots *)dd;
	this->z = this->dingle_dots->next_z++;
	this->label = new string(label);
	this->r = r;
	this->color_normal = color_copy(&c);
	this->color_on = color_lighten(&c, 0.95);
}

int SnapshotShape::set_on() {
	this->on = 1;
	return 1;
}

int SnapshotShape::set_off() {
	this->on = 0;
	this->dingle_dots->do_snapshot = 1;
	return 1;
}

bool SnapshotShape::render(std::vector<cairo_t *> &contexts) {
	color *c;
	char text_to_add[NCHAR];
	memset(text_to_add, '\0', sizeof(text_to_add));
	c = &this->color_normal;
	for (std::vector<cairo_t *>::iterator it = contexts.begin(); it != contexts.end(); ++it) {
		cairo_t *cr = *it;
		cairo_save(cr);
		cairo_translate(cr, this->pos.x, this->pos.y);
		cairo_scale(cr, this->scale, this->scale);
		cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a);
		cairo_arc(cr, 0, 0, this->r*0.975, 0, 2. * M_PI);
		cairo_fill(cr);
		cairo_arc(cr, 0, 0, this->r, 0, 2*M_PI);
		cairo_set_source_rgba(cr, 0.5*c->r, 0.5*c->g, 0.5*c->b, c->a);
		cairo_set_line_width(cr, 0.05 * this->r);
		cairo_stroke(cr);
		if (this->on) {
			sprintf(text_to_add, "\nIN\n%.00f",
					ceil(this->countdown_radius_easer.time_left_secs()));
			cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
			cairo_arc(cr, 0, 0, this->radius_on, 0, 2*M_PI);
			cairo_fill(cr);
		}
		if (this->hovered) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
			cairo_arc(cr, 0, 0, this->r, 0, 2*M_PI);
			cairo_fill(cr);
		}
		if (this->selected) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
			cairo_arc(cr, 0, 0, this->r*1.025, 0, 2*M_PI);
			cairo_fill(cr);
		}
		this->render_label(cr, text_to_add);
		cairo_restore(cr);
	}
	return true;
}

int beep_cb(gpointer data) {
	DingleDots *dd = (DingleDots *)data;
	ca_context_play(dd->event_sound_ctx, 0,
		CA_PROP_EVENT_ID, "window-attention",
		CA_PROP_EVENT_DESCRIPTION, "window attention",
		NULL);
	return FALSE;
}

void SnapshotShape::set_motion_state(uint8_t state) {
	double final_radius = this->r;
	if (state && this->motion_state == 0) {
		this->motion_state = 1;
		this->motion_state_to_off = 0;
		this->countdown_radius_easer.initialize(this->dingle_dots,
												this,
												EASER_LINEAR,
												std::bind(&SnapshotShape::set_radius_on,
														  this, std::placeholders::_1),
												final_radius, 0.0,
												SHUTDOWN_SECS);
		this->countdown_radius_easer.start();
		for (int i = 0; i < SHUTDOWN_SECS; ++i) {
			g_timeout_add(i * 1000, beep_cb, this->dingle_dots);
		}
		clock_gettime(CLOCK_MONOTONIC, &this->motion_ts);
	} else {
		if (this->motion_state) {
			if (this->countdown_radius_easer.done()) {
				this->motion_state = 0;
				this->motion_state_to_off = 0;
			} else {
				this->motion_state_to_off = 1;
			}
		}
	}
}

double SnapshotShape::get_radius_on() const
{
	return radius_on;
}

void SnapshotShape::set_radius_on(double value)
{
	radius_on = value;
	gtk_widget_queue_draw(this->dingle_dots->drawing_area);
}
