#include "sound_shape.h"
#include "midi.h"

uint8_t major[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
uint8_t minor[] = { 0, 2, 3, 5, 7, 8, 10, 12 };

int sound_shape_init(sound_shape *ss, char *label,
 uint8_t midi_note, double x, double y, double r,
 color *c, dingle_dots_t *dd) {
  ss->dd = dd;
	ss->active = 0;
  ss->x = x;
  ss->y = y;
  ss->r = r;
  ss->z = dd->next_z++;
  strncpy(ss->label, label, NCHAR);
  ss->midi_note = midi_note;
  ss->normal = color_copy(c);
  ss->playing = color_lighten(c, 0.95);
  ss->on = 0;
  ss->mdown = 0;
  return 0;
}

static void sound_shape_render_label(sound_shape *ss, cairo_t *cr) {
  PangoLayout *layout;
  PangoFontDescription *desc;
  int width, height;
  char font[32];
  sprintf(font, "Agave %d", (int)ceil(0.2 * ss->r));
  layout = pango_cairo_create_layout(cr);
  pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
  pango_layout_set_text(layout, ss->label, -1);
  desc = pango_font_description_from_string(font);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  cairo_save(cr);
  ss->on ? cairo_set_source_rgba(cr, 0., 0., 0., ss->playing.a) :
   cairo_set_source_rgba(cr, 1., 1., 1., ss->normal.a);
  pango_layout_get_size(layout, &width, &height);
  cairo_translate(cr, ss->x - 0.5*width/PANGO_SCALE, ss->y
   - 0.5*height/PANGO_SCALE);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  g_object_unref(layout);
}

int sound_shape_render(sound_shape *ss, cairo_t *cr) {
  color *c;
  c = &ss->normal;
  cairo_save(cr);
	cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a);
  cairo_translate(cr, ss->x, ss->y);
  cairo_arc(cr, 0, 0, ss->r*0.975, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_arc(cr, 0, 0, ss->r, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 0.5*c->r, 0.5*c->g, 0.5*c->b, c->a);
  cairo_set_line_width(cr, 0.05 * ss->r);
	cairo_stroke(cr);
	if (ss->on) {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
	 	cairo_arc(cr, 0, 0, ss->r*1.025, 0, 2 * M_PI);
	  cairo_fill(cr);
	}
	if (ss->hovered) {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
  	cairo_arc(cr, 0, 0, ss->r, 0, 2 * M_PI);
		cairo_set_line_width(cr, 0.05 * ss->r);
  	cairo_stroke(cr);
		/*cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
	 	cairo_arc(cr, 0, 0, ss->r * 1.025, 0, 2 * M_PI);
  	cairo_fill(cr);*/
	}
	if (ss->selected) {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
	 	cairo_arc(cr, 0, 0, ss->r*1.025, 0, 2 * M_PI);
	  cairo_fill(cr);
	}
  cairo_restore(cr);
  sound_shape_render_label(ss, cr);
  return 0;
}

int sound_shape_activate(sound_shape *ss) {
  ss->active = 1;
  return 0;
}

int sound_shape_deactivate(sound_shape *ss) {
	if (ss->on) {
		sound_shape_off(ss);
	}
	ss->active = 0;
  return 0;
}

int sound_shape_in(sound_shape *ss, double x, double y) {
  if (sqrt(pow((x - ss->x), 2) + pow(y - ss->y, 2)) <= ss->r) {
    return 1;
  } else {
    return 0;
  }
}

int sound_shape_on(sound_shape *ss) {
  ss->on = 1;
  midi_queue_new_message(0x90, ss->midi_note, 64, ss->dd);
  return 0;
}

int sound_shape_off(sound_shape *ss) {
  ss->on = 0;
  midi_queue_new_message(0x80, ss->midi_note, 0, ss->dd);
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
