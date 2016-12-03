#if !defined (_SOUND_SHAPE_H)
#define _SOUND_SHAPE_H (1)

#include <cairo/cairo.h>
#include <math.h>
#include <stdint.h>
#include "midi.h"

typedef struct {
  double r;
  double g;
  double b;
  double a;
} color;

typedef struct {
  double x;
  double y;
  double r;
  uint8_t on;
  uint8_t midi_note;
  color normal;
  color playing;
} sound_shape;

int sound_shape_init(sound_shape *ss, uint8_t midi_note,
 double x, double y, double r,
 int red, int g, int b, double a);
int sound_shape_render(sound_shape *ss, cairo_t *cr);
int sound_shape_in(sound_shape *ss, double x, double y);
int sound_shape_on(sound_shape *ss);
int sound_shape_off(sound_shape *ss);

#endif
