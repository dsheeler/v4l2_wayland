#if !defined(_DINGLE_DOTS_H)
#define _DINGLE_DOTS_H (1)

#include <gtk/gtk.h>
#include "v4l2_wayland.h"
#include "sound_shape.h"
#include "kmeter.h"
#include "midi.h"
#include "v4l2.h"
#include "video_file_source.h"

#define STR_LEN 80
#define MAX_NUM_V4L2 2

typedef struct sound_shape sound_shape;
typedef struct dingle_dots_t dingle_dots_t;
typedef struct midi_key_t midi_key_t;
typedef struct dd_v4l2_t dd_v4l2_t;

struct dingle_dots_t {
  GApplication *app;
	gboolean fullscreen;
	char video_file_name[STR_LEN];
	int recording_started;
	int recording_stopped;
	int can_process;
	int can_capture;
	int audio_done;
	int video_done;
  int trailer_written;
  int make_new_tld;
	int use_rand_color_for_scale;
  int shift_pressed;
	int delete_active;
	uint32_t video_bitrate;
	AVFormatContext *video_output_context;
	struct timespec out_frame_ts;
  disk_thread_info_t audio_thread_info;
  disk_thread_info_t video_thread_info;
	disk_thread_info_t snapshot_thread_info;
	AVFrame *sources_frame;
	AVFrame *drawing_frame;
	AVFrame *analysis_frame;
  struct SwsContext *analysis_resize;
 	AVFrame *screen_frame;
  struct SwsContext *screen_resize;
 	AVFrame *video_frame;
	double scale;
	video_file_t vf[MAX_NUM_VIDEO_FILES];
	int current_video_file_source_index;
	dd_v4l2_t dd_v4l2[MAX_NUM_V4L2];
	sound_shape sound_shapes[MAX_NSOUND_SHAPES];
  kmeter meters[2];
	GdkRectangle drawing_rect;
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
  GtkWidget *ctl_window;
	GtkWidget *scale_combo;
	GtkWidget *note_combo;
	GtkWidget *rand_color_button;
	GtkWidget *scale_color_button;
	GtkWidget *record_button;
	GtkWidget *delete_button;
	cairo_surface_t *csurface;
  cairo_t *cr;
  long jack_overruns;
	int nports;
	jack_default_audio_sample_t **in;
	jack_default_audio_sample_t **out;
  jack_port_t **in_ports;
	jack_port_t **out_ports;
	jack_client_t *client;
	jack_port_t *midi_port;
	jack_ringbuffer_t *midi_ring_buf;
};

int dingle_dots_init(dingle_dots_t *dd, char *dev_name, int width, int height,
 char *video_file_name, int video_bitrate);
int dingle_dots_free(dingle_dots_t *dd);
int dingle_dots_deactivate_sound_shapes(dingle_dots_t *dd);
int dingle_dots_add_note(dingle_dots_t *dd, char *scale_name,
 int scale_num, int midi_note, int midi_channel, double x, double y, double r, color *c);
void dingle_dots_add_scale(dingle_dots_t *dd, midi_key_t *key, int midi_channel,
 color *c);
#endif
