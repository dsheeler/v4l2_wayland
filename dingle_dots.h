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
#include "v4l2_wayland.h"
typedef struct midi_key_t midi_key_t;
#include "sound_shape.h"
#include "snapshot_shape.h"
#include "kmeter.h"
#include "midi.h"
#include "video_file_source.h"
#include "v4l2.h"
#include "sprite.h"

#define STR_LEN 80
#define MAX_NUM_V4L2 4

class DingleDots {
public:
	DingleDots();
	int init(char *dev_name, int width, int height,
			 char *video_file_name, int video_bitrate);
	int free();
	int deactivate_sound_shapes();
	int add_note(char *scale_name,
				 int scale_num, int midi_note, int midi_channel,
				 double x, double y, double r, color *c);
	void add_scale(midi_key_t *key, int midi_channel,
				   color *c);
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
	uint8_t animating;
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
	VideoFile vf[MAX_NUM_VIDEO_FILES];
	int current_video_file_source_index;
	int current_sprite_index;
	V4l2 v4l2[MAX_NUM_V4L2];
	Sprite sprites[MAX_NUM_SPRITES];
	SnapshotShape snapshot_shape;
	SoundShape sound_shapes[MAX_NUM_SOUND_SHAPES];
	kmeter meters[2];
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
	GtkWidget *ctl_window;
	GtkWidget *drawing_area;
	GtkWidget *scale_combo;
	GtkWidget *note_combo;
	GtkWidget *rand_color_button;
	GtkWidget *scale_color_button;
	GtkWidget *record_button;
	GtkWidget *delete_button;
	long jack_overruns;
	int nports;
	jack_default_audio_sample_t **in;
	jack_default_audio_sample_t **out;
	jack_port_t **in_ports;
	jack_port_t **out_ports;
	jack_client_t *client;
	jack_port_t *midi_port;
	jack_ringbuffer_t *midi_ring_buf;
	color random_color();
	uint8_t get_animating() const;
	void set_animating(const uint8_t &value);
	void get_sound_shapes(std::vector<Drawable *> &sound_shapes);
	void get_sources(std::vector<Drawable *> &list);
};

#endif
