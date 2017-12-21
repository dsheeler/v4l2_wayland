#include "sound_shape.h"
#include "midi.h"

SoundShape::SoundShape() { }
void SoundShape::init(string &label, uint8_t midi_note, uint8_t midi_channel,
					  double x, double y, double r, color *c, DingleDots *dd) {
	this->clear_state();
	this->pos.x = x;
	this->pos.y = y;
	this->z = dd->next_z++;
	this->dd = dd;
	this->active = 0;
	this->r = r;
	if (this->label) delete this->label;
	this->label = new string(label);
	this->midi_note = midi_note;
	this->midi_channel = midi_channel;
	this->normal = color_copy(c);
	this->playing = color_lighten(c, 0.95);
	this->on = 0;
}

int SoundShape::is_on() {
	return this->on;
}

void SoundShape::render_label(cairo_t *cr) {
	PangoLayout *layout;
	PangoFontDescription *desc;
	int width, height;
	char font[32];
	sprintf(font, "Agave %d", (int)ceil(0.2 * this->r));
	layout = pango_cairo_create_layout(cr);
	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_text(layout, this->label->c_str(), -1);
	desc = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	cairo_save(cr);
	this->is_on() ? cairo_set_source_rgba(cr, 0., 0., 0., this->playing.a) :
					cairo_set_source_rgba(cr, 1., 1., 1., this->normal.a);
	pango_layout_get_size(layout, &width, &height);
	cairo_translate(cr, this->pos.x - 0.5*width/PANGO_SCALE, this->pos.y
					- 0.5*height/PANGO_SCALE);
	pango_cairo_show_layout(cr, layout);
	cairo_restore(cr);
	g_object_unref(layout);
}

bool SoundShape::render(std::vector<cairo_t *> &contexts) {
	color *c;
	c = &this->normal;

	for (std::vector<cairo_t *>::iterator it = contexts.begin(); it != contexts.end(); ++it) {
		cairo_t *cr = *it;
		cairo_save(cr);
		cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a);
		cairo_translate(cr, this->pos.x, this->pos.y);
		cairo_arc(cr, 0, 0, this->r*0.975, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_arc(cr, 0, 0, this->r, 0, 2 * M_PI);
		cairo_set_source_rgba(cr, 0.5*c->r, 0.5*c->g, 0.5*c->b, c->a);
		cairo_set_line_width(cr, 0.05 * this->r);
		cairo_stroke(cr);
		if (this->on) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
			cairo_arc(cr, 0, 0, this->r*1.025, 0, 2 * M_PI);
			cairo_fill(cr);
		}
		if (this->hovered) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
			cairo_arc(cr, 0, 0, this->r, 0, 2 * M_PI);
			cairo_set_line_width(cr, 0.05 * this->r);
			cairo_stroke(cr);
			/*cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
		cairo_arc(cr, 0, 0, this->r * 1.025, 0, 2 * M_PI);
	cairo_fill(cr);*/
		}
		if (this->selected) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
			cairo_arc(cr, 0, 0, this->r*1.025, 0, 2 * M_PI);
			cairo_fill(cr);
		}
		cairo_restore(cr);
		this->render_label(cr);
	}
	return true;
}

int SoundShape::activate() {
	this->active = 1;
	return 0;
}

void SoundShape::clear_state() {
	this->selected = 0;
	this->hovered = 0;
	this->motion_state = 0;
	this->tld_state = 0;
	this->mdown = 0;
	this->active = 0;
}

int SoundShape::deactivate() {
	if (this->on) {
		this->set_off();
	}
	this->clear_state();
	return 0;
}

int SoundShape::in(double x, double y) {
	if (sqrt(pow((x - this->pos.x), 2) + pow(y - this->pos.y, 2)) <= this->r) {
		return 1;
	} else {
		return 0;
	}
}

int SoundShape::set_on() {
	this->on = 1;
	midi_queue_new_message(0x90 | this->midi_channel, this->midi_note, 64, this->dd);
	return 0;
}

int SoundShape::set_off() {
	this->on = 0;
	this->double_clicked_on = 0;
	midi_queue_new_message(0x80 | this->midi_channel, this->midi_note, 0, this->dd);
	return 0;
}

void SoundShape::tick() {
	if (this->motion_state_to_off) {
		this->set_motion_state(0);
	}
}

void SoundShape::set_motion_state(uint8_t state) {
	if (state) {
		this->motion_state = 1;
		this->motion_state_to_off = 0;
		clock_gettime(CLOCK_MONOTONIC, &this->motion_ts);
	} else {
		if (this->motion_state) {
			struct timespec now_ts;
			struct timespec diff_ts;
			double diff_sec;
			clock_gettime(CLOCK_MONOTONIC, &now_ts);
			timespec_diff(&this->motion_ts, &now_ts, &diff_ts);
			diff_sec = timespec_to_seconds(&diff_ts);
			if (diff_sec >= 0.2) {
				this->motion_state = 0;
				this->motion_state_to_off = 0;
			} else {
				this->motion_state_to_off = 1;
			}
		}
	}
}

int color_init(color *c, double r, double g, double b, double a) {
	c->r = r;
	c->g = g;
	c->b = b;
	c->a = a;
	return 0;
}

color color_copy(color *in) {
	color out;
	out.r = in->r;
	out.g = in->g;
	out.b = in->b;
	out.a = in->a;
	return out;
}

struct hsva rgb2hsv(color *c) {
	struct hsva out;
	double max = fmax(fmax(c->r, c->g), c->b);
	double min = fmin(fmin(c->r, c->g), c->b);
	double h, s, v;
	double r, g, b;
	r = c->r;
	g = c->g;
	b = c->b;
	h = s = v = max;
	double d = max - min;
	s = max == 0 ? 0 : d / max;
	if(max == min){
		h = 0; // achromatic
	} else {
		if (r == max) {
			h = (g - b) / d + (g < b ? 6 : 0);
		} else if (g == max) {
			h = (b - r) / d + 2;
		} else if (b == max) {
			h = (r - g) / d + 4;
		}
		h /= 6;
	}
	out.h = h;
	out.s = s;
	out.v = v;
	out.a = c->a;
	return out;
}

color hsv2rgb(struct hsva *in) {
	color out;
	double r, g, b;
	double h, s, v;
	h = in->h;
	s = in->s;
	v = in->v;
	int i = (int)floor(h * 6);
	double f = h * 6 - i;
	double p = v * (1 - s);
	double q = v * (1 - f * s);
	double t = v * (1 - (1 - f) * s);
	switch(i % 6){
		case 0: r = v, g = t, b = p; break;
		case 1: r = q, g = v, b = p; break;
		case 2: r = p, g = v, b = t; break;
		case 3: r = p, g = q, b = v; break;
		case 4: r = t, g = p, b = v; break;
		case 5: r = v, g = p, b = q; break;
	}
	color_init(&out, r, g, b, in->a);
	return out;
}

color color_lighten(color *in, double mag) {
	color out;
	struct hsva lighter;
	lighter = rgb2hsv(in);
	lighter.v = lighter.v + (1 - lighter.v) * mag;
	out = hsv2rgb(&lighter);
	return out;
}
