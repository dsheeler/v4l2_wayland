#if !defined (_SOUND_SHAPE_H)
#define _SOUND_SHAPE_H (1)

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <gdk/gdk.h>

#include "v4l2_wayland.h"

#define NCHAR 32
#define MAX_NSOUND_SHAPES 256

typedef struct sound_shape sound_shape;
typedef struct dingle_dots_t dingle_dots_t;

typedef struct {
  double r;
  double g;
  double b;
  double a;
} color;

struct hsva {
  double h;
  double s;
  double v;
  double a;
};

struct sound_shape {
	dingle_dots_t *dd;
	uint8_t active;
  uint8_t on;
	uint8_t double_clicked_on;
	uint8_t selected;
	uint8_t hovered;
	GdkPoint selected_pos;
	double x;
  double y;
  double r;
  uint64_t z;
  char label[NCHAR];
  uint8_t midi_note;
  color normal;
  color playing;
	uint8_t mdown;
  GdkPoint mdown_pos;
  GdkPoint down_pos;
};

int sound_shape_init(sound_shape *ss, char *label,
 uint8_t midi_note, double x, double y, double r,
 color *c, dingle_dots_t *dd);
int sound_shape_render(sound_shape *ss, cairo_t *cr);
int sound_shape_activate(sound_shape *ss);
int sound_shape_deactivate(sound_shape *ss);
int sound_shape_in(sound_shape *ss, double x, double y);
int sound_shape_on(sound_shape *ss);
int sound_shape_off(sound_shape *ss);
int color_init(color *c, double r, double g, double b, double a);
color color_copy(color *c);
struct hsva rgb2hsv(color *c);
color hsv2rgb(struct hsva *in);
color color_lighten(color *in, double mag);
extern uint64_t next_z;

#endif
