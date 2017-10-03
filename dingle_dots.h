#if !defined(_DINGLE_DOTS_H)
#define _DINGLE_DOTS_H (1)

#include <gtk/gtk.h>
#include "v4l2_wayland.h"
#include "sound_shape.h"
#include "kmeter.h"
#include "midi.h"

typedef struct sound_shape sound_shape;
typedef struct dingle_dots_t dingle_dots_t;
typedef struct midi_key_t midi_key_t;

struct dingle_dots_t {
  GApplication *app;
	gboolean fullscreen;
	struct timespec out_frame_ts;
  disk_thread_info_t audio_thread_info;
  disk_thread_info_t video_thread_info;
	disk_thread_info_t snapshot_thread_info;
	AVFrame *drawing_frame;
	AVFrame *analysis_frame;
  struct SwsContext *analysis_resize;
 	AVFrame *screen_frame;
  struct SwsContext *screen_resize;
 	sound_shape sound_shapes[MAX_NSOUND_SHAPES];
  kmeter meters[2];
 	GdkRectangle camera_rect;
	int doing_motion;
	int doing_tld;
	uint8_t do_snapshot;
	GdkRectangle user_tld_rect;
	float motion_threshold;
	uint8_t selection_in_progress;
  GdkRectangle analysis_rect;
  double ascale_factor_x;
	double ascale_factor_y;
	GdkRectangle selection_rect;
	uint64_t next_z;
	GdkPoint mouse_pos;
	uint8_t mdown;
	uint8_t dragging;
	uint8_t smdown;
	GdkPoint mdown_pos;
	GtkWidget *scale_combo;
	GtkWidget *note_combo;
	cairo_surface_t *csurface;
  cairo_t *cr;
	jack_client_t *client;
	jack_port_t *midi_port;
	jack_ringbuffer_t *midi_ring_buf;
};

int dingle_dots_init(dingle_dots_t *dd, int width, int height);
int dingle_dots_free(dingle_dots_t *dd);
int dingle_dots_deactivate_sound_shapes(dingle_dots_t *dd);
int dingle_dots_add_note(dingle_dots_t *dd, char *scale_name,
 int scale_num, int midi_note, double x, double y, double r, color *c);
void dingle_dots_add_scale(dingle_dots_t *dd, midi_key_t *key);
#endif
