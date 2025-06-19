#if !defined(_DINGLE_DOTS_H)
#define _DINGLE_DOTS_H (1)

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#ifdef __cplusplus
}
#endif
#include <gtk/gtk.h>
#include <gtkmm-3.0/gtkmm.h>
#include <canberra.h>
#include "v4l2_wayland.h"
#include <fftw3.h>
typedef struct midi_key_t midi_key_t;
#include "sound_shape.h"
#include "snapshot_shape.h"
#include "kmeter.h"
#include "midi.h"
#include "video_file_source.h"
#include "v4l2.h"
#include "sprite.h"
#include "easable.h"
#include "video_file_out.h"
#include "x11.h"
#include "text.h"
#include "hex.h"

#define STR_LEN 80
#define MAX_NUM_V4L2 8
#define MAX_NUM_VIDEO_FILES 8
#define MAX_NUM_SPRITES 32
#define MAX_NUM_SOUND_SHAPES 128
#define MAX_NUM_TEXTS 32
#define MAX_NUM_X11 8

class DingleDots : public Easable {
public:
	DingleDots();
	int init(int width, int height);
	int free();
	int deactivate_sound_shapes();
	int setup_jack();
	int add_note(char *scale_name,
				 int scale_num, int midi_note, int midi_channel,
				 double x, double y, double r, color *c);
	void add_scale(midi_key_t *key, int midi_channel,
				   color *c);
	GApplication *app;
	gboolean fullscreen;
	int can_process;
	int can_capture;
	int make_new_tld;
	int use_rand_color_for_scale;
	int use_rand_color_for_text;
	int use_window_x11;
	int shift_pressed;
	int delete_active;
	double selection_box_alpha;
	uint8_t animating;
	disk_thread_info_t snapshot_thread_info;
	AVFrame *sources_frame;
	AVFrame *drawing_frame;
	AVFrame *analysis_frame;
	struct SwsContext *analysis_resize;
	AVFrame *screen_frame;
	struct SwsContext *screen_resize;
	AVFrame *video_frame;
	double scale;
	VideoFile vf[MAX_NUM_VIDEO_FILES];
	VideoFileOut vfo[MAX_NUM_VIDEO_FILES];
	int current_video_file_source_index;
	int current_sprite_index;
	V4l2 v4l2[MAX_NUM_V4L2];
	Sprite background;
	Sprite sprites[MAX_NUM_SPRITES];
	X11 x11[MAX_NUM_X11];
	Text text[MAX_NUM_TEXTS];
    Hex hexes[MAX_NUM_TEXTS];
	SnapshotShape snapshot_shape;
	SoundShape sound_shapes[MAX_NUM_SOUND_SHAPES];
	Meter meters[2];
	GdkRectangle drawing_rect;
	int doing_motion;
	int doing_tld;
	uint8_t show_shapshot_shape;
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
	uint8_t s_pressed;
	GdkPoint mdown_pos;
	GdkPoint mup_pos;
	GtkWidget *ctl_window;
	GtkWidget *drawing_area;
	GtkWidget *scale_combo;
	GtkWidget *note_combo;
	GtkWidget *x11_windows_combo;
	GtkWidget *rand_color_button;
	GtkWidget *scale_color_button;
	GtkWidget *record_button;
	GtkWidget *bitrate_entry;
	GtkWidget *delete_button;
	GtkWidget *channel_combo;
	GtkWidget *x11_x_input;
	GtkWidget *x11_y_input;
	GtkWidget *x11_w_input;
	GtkWidget *x11_h_input;
	GtkWidget *x11_win_button;
	GtkWidget *text_entry;
	Gdk::RGBA text_color;
	Gtk::ColorButton *text_color_button;
	Gtk::Entry *text_font_entry;
	Gtk::SpinButton *text_font_size_input;
	ca_context *event_sound_ctx;
	long jack_overruns;
	int nports;
	fftw_complex *fftw_in, *fftw_out;
	fftw_plan p;
	jack_default_audio_sample_t **in;
	jack_default_audio_sample_t **out;
	jack_port_t **in_ports;
	jack_port_t **out_ports;
	jack_client_t *client;
	jack_port_t *midi_port;
	jack_ringbuffer_t *midi_ring_buf;
	color random_color();
	uint8_t get_animating() const;
    void toggle_fullscreen();
	void set_animating(const uint8_t &value);
	void get_sound_shapes(std::vector<vwDrawable *> &sound_shapes);
	void get_sources(std::vector<vwDrawable *> &list);
	double get_selection_box_alpha() const;
	void set_selection_box_alpha(double value);
	void render_selection_box(cairo_t *cr);
	void set_selecting_on();
	void set_selecting_off();
	void queue_draw();
	vwColor random_vw_color();
};

#endif
