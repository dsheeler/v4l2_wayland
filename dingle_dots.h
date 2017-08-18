#if !defined(_DINGLE_DOTS_H)
#define _DINGLE_DOTS_H (1)

#include <gtk/gtk.h>
#include "v4l2_wayland.h"
#include "sound_shape.h"
#include "kmeter.h"

typedef struct sound_shape sound_shape;
typedef struct dingle_dots_t dingle_dots_t;

struct dingle_dots_t {
  GApplication *app;
	int width;
	int height;
  disk_thread_info_t *audio_thread_info;
  disk_thread_info_t *video_thread_info;
  sound_shape sound_shapes[MAX_NSOUND_SHAPES];
  kmeter meters[2];
	int doing_motion;
	int doing_tld;
	float motion_threshold;
	cairo_surface_t *csurface;
  cairo_t *cr;
	AVFrame *screen_frame;
	jack_client_t *client;
	jack_port_t *midi_port;
};

int dingle_dots_init(dingle_dots_t *dd, midi_key_t *keys, uint8_t nb_keys,
 int width, int height);
int dingle_dots_add_note(dingle_dots_t *dd, int scale_num, int midi_note,
 double x, double y, double r, color *c);
#endif
