#include "sound_shape.h"

int sound_shape_init(sound_shape *ss, char *label,
 uint8_t midi_note, double x, double y, double r,
 int red, int g, int b, double a) {
  ss->x = x;
  ss->y = y;
  ss->r = r;
  strncpy(ss->label, label, NCHAR);
  ss->midi_note = midi_note;
  ss->normal.r = red/255.;
  ss->normal.g = g/255.;
  ss->normal.b = b/255.;
  ss->normal.a = a;
  ss->playing.r = 30/255.;
  ss->playing.g = 240/255.;
  ss->playing.b = 180/255.;
  ss->playing.a = a;
  ss->on = 0;
  ss->mdown = 0;
  return 0;
}

static void sound_shape_render_label(sound_shape *ss, cairo_t *cr) {
#define FONT "Agave 18"
  PangoLayout *layout;
  PangoFontDescription *desc;
  int width, height;
  layout = pango_cairo_create_layout(cr);
  pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
  pango_layout_set_text(layout, ss->label, -1);
  desc = pango_font_description_from_string(FONT);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  cairo_save(cr);
  ss->on ? cairo_set_source_rgba(cr, 0.15, 0.15, 0.15, ss->playing.a) :
   cairo_set_source_rgba(cr, 0.85, 0.85, 0.85, ss->normal.a);
  pango_layout_get_size(layout, &width, &height);
  cairo_translate(cr, ss->x - 0.5*width/PANGO_SCALE, ss->y
   - 0.5*height/PANGO_SCALE);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  g_object_unref(layout);
}

int sound_shape_render(sound_shape *ss, cairo_t *cr) {
  color *c;
  c = ss->on ? &ss->playing : &ss->normal;
  cairo_save(cr);
  cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a);
  cairo_translate(cr, ss->x, ss->y);
  cairo_arc(cr, 0, 0, ss->r, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_restore(cr);
  sound_shape_render_label(ss, cr);
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
  queue_new_message(0x90, ss->midi_note, 64);
  return 0;
}

int sound_shape_off(sound_shape *ss) {
  ss->on = 0;
  queue_new_message(0x80, ss->midi_note, 0);
  return 0;
}

