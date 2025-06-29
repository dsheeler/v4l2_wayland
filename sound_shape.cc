#include <utility>
#include <functional>
#include "sound_shape.h"
#include "midi.h"

SoundShape::SoundShape() { active = 0; }

void SoundShape::init(char *label, uint8_t midi_note, uint8_t midi_channel,
					  double x, double y, double r, color *c, DingleDots *dd) {
	this->clear_state();
	this->pos.x = x;
	this->pos.y = y;
	this->dingle_dots = dd;
	this->z = dd->next_z++;
	this->active = 0;
	this->scale = 1.0;
	this->r = r;
	this->double_clicked_on = 0;
	this->opacity = 1.0;
	this->label = new string(label);
	this->midi_note = midi_note;
	this->midi_channel = midi_channel;
	this->color_normal = color_copy(c);
	this->color_on = color_lighten(c, 0.95);
	this->shutdown_time = 0.2;
	this->on = 0;

}

int SoundShape::is_on() {
	return this->on;
}

bool SoundShape::render(std::vector<cairo_t *> &contexts) {
	color *c;
	c = &this->color_normal;
	for (std::vector<cairo_t *>::iterator it = contexts.begin(); it != contexts.end(); ++it) {
		cairo_t *cr = *it;
		cairo_save(cr);
		cairo_translate(cr, this->pos.x, this->pos.y);
		cairo_scale(cr, this->scale, this->scale);
		cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a);
		cairo_arc(cr, 0, 0, this->r*0.95, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_arc(cr, 0, 0, this->r * 0.975, 0, 2 * M_PI);
		cairo_set_source_rgba(cr, 0.5*c->r, 0.5*c->g, 0.5*c->b, c->a);
		cairo_set_line_width(cr, 0.05 * this->r);
		cairo_stroke(cr);
		if (this->on) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
			cairo_arc(cr, 0, 0, this->r*1.0, 0, 2 * M_PI);
			cairo_fill(cr);
		}
		if (this->hovered) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
			cairo_arc(cr, 0, 0, this->r, 0, 2 * M_PI);
			cairo_fill(cr);
		}
		if (this->selected) {
			cairo_set_source_rgba(cr, 0, 0, 0, 0.6);
			cairo_arc(cr, 0, 0, this->r, 0, 2 * M_PI);
			cairo_fill(cr);
		}
		this->render_label(cr, "");
		cairo_restore(cr);
	}
	return true;
}

void SoundShape::render_label(cairo_t *cr, const char *text_to_append) {
	PangoLayout *layout;
	PangoFontDescription *desc;
	int width, height;
	char font[32];
	std::string label_final	= *this->label + std::string(text_to_append);
	sprintf(font, "Agave %d", (int)floor(0.2 * this->r));
	layout = pango_cairo_create_layout(cr);
	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_text(layout, label_final.c_str(), -1);
	desc = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	cairo_save(cr);
	this->is_on() ? cairo_set_source_rgba(cr, 1., 1., 1., this->color_on.a) :
					cairo_set_source_rgba(cr, 1., 1., 1., this->color_normal.a);
	pango_layout_get_size(layout, &width, &height);
	cairo_translate(cr, - 0.5*width/PANGO_SCALE,
					- 0.5*height/PANGO_SCALE);
	pango_cairo_show_layout(cr, layout);
	cairo_restore(cr);
	g_object_unref(layout);
}

void SoundShape::clear_state() {
	this->selected = 0;
	this->hovered = 0;
	this->motion_state = 0;
	this->tld_state = 0;
	this->mdown = 0;
	this->active = 0;
}

void SoundShape::deactivate_action() {
	if (active) {
		if (this->on) {
			this->set_off();
		}
		this->clear_state();
		gtk_widget_queue_draw(dingle_dots->drawing_area);
	}
}


int SoundShape::in(double x, double y) {
	if (sqrt(pow((x - this->pos.x), 2) + pow(y - this->pos.y, 2)) <= this->r * this->scale) {
		return 1;
	} else {
		return 0;
	}
}

int SoundShape::set_on() {
	this->on = 1;
	midi_queue_new_message(0x90 | this->midi_channel, this->midi_note, 64, this->dingle_dots);
	gtk_widget_queue_draw(dingle_dots->drawing_area);
	return 0;
}

int SoundShape::set_off() {
	this->on = 0;
	this->double_clicked_on = 0;
	midi_queue_new_message(0x80 | this->midi_channel, this->midi_note, 0, this->dingle_dots);
	gtk_widget_queue_draw(dingle_dots->drawing_area);
	return 0;
}

void SoundShape::tick() {
	return;
	if (this->motion_state_to_off) {
		this->set_motion_state(0);
	}
}

double SoundShape::get_secs_since_last_on()
{
	struct timespec now_ts;
	struct timespec diff_ts;
	double diff_sec;
	clock_gettime(CLOCK_MONOTONIC, &now_ts);
	timespec_diff(&this->motion_ts, &now_ts, &diff_ts);
	diff_sec = timespec_to_seconds(&diff_ts);
	return diff_sec;
}

void SoundShape::set_motion_state(uint8_t state) {
	if (state) {
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
double fRand(double fMin, double fMax) {
    double f = (double)rand() / RAND_MAX;
    return fMin + f * (fMax - fMin);
}

int SoundShape::activate() {
	if (!this->active) {
		this->easers.erase(this->easers.begin(), this->easers.end());
		double duration = 4;
		double final_x, final_y;
		double start_x, start_y;
		final_x = this->pos.x;
		final_y = this->pos.y;
		double w = this->dingle_dots->drawing_rect.width;
		double h = this->dingle_dots->drawing_rect.height;
		double r_min = (sqrt(pow(w, 2) + pow(h,2)));
		double r_max = r_min;
		double r = fRand(r_min, r_max);
		double theta = fRand(0, 2 * M_PI);
		start_x = r * cos(theta);
		start_y = r * sin(theta);
		duration += fRand(-2, 2);
		Easer *er = new Easer();
		er->initialize(this->dingle_dots, this, EASER_ELASTIC_EASE_OUT, std::bind(&vwDrawable::set_x, this, std::placeholders::_1), start_x, final_x, duration);
		Easer *er2 = new Easer();
		er2->initialize(this->dingle_dots, this, EASER_ELASTIC_EASE_OUT, std::bind(&vwDrawable::set_y, this, std::placeholders::_1), start_y, final_y, duration);
		this->active = 1;
		er->start();
		er2->start();
	}
	return 0;
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
