#if !defined (_KMETER_H)
#define _KMETER_H (1)

#include <cairo.h>
#include "v4l2_wayland.h"
#include "sound_shape.h"

typedef struct kmeter {
  float  z1;          // filter state
  float  z2;          // filter state
  float  rms;         // max rms value since last read()
  float  dpk;         // current digital peak value
  int    cnt;	       // digital peak hold counter
  int    flag;        // flag set by read(), resets _rms
  int    hold;
  float  fall;
  float  omega;
  color  c;
  float  x;
  float  y;
  float  w;
} kmeter;

void kmeter_init(kmeter *km, int fsamp, int fsize, float hold, float fall,
 float x, float y, float w, color c);
void kmeter_process(kmeter *km, float *p, int n);
void kmeter_read(kmeter *km, float *rms, float *dpk);
void kmeter_render(kmeter *km, cairo_t *cr, float opacity);

#endif
