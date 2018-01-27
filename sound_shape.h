#if !defined (_SOUND_SHAPE_H)
#define _SOUND_SHAPE_H (1)

typedef struct color color;

#include "v4l2_wayland.h"
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <math.h>
#include <string.h>
#include <string>
#include <stdint.h>
#include <gdk/gdk.h>
#include "drawable.h"
//#include "dingle_dots.h"

#define NCHAR 32
#define MAX_NUM_SOUND_SHAPES 256

#ifdef __cplusplus
using namespace std;
#endif
class DingleDots;
class SoundShape : public Drawable {
public:
	SoundShape();
	virtual void init(char *label, uint8_t midi_note, uint8_t midi_channel,
			  double x, double y, double r, color *c, DingleDots *dd);
	bool virtual render(std::vector<cairo_t *> &contexts);
	void render_label(cairo_t *cr, char *text_to_append);
	void deactivate_action();
	int in(double x, double y);
	int virtual set_on();
	virtual int set_off();
	void tick();
	int is_on();
	void clear_state();
	virtual void set_motion_state(uint8_t state);
	//	private:
	double shutdown_time;
	uint8_t on;
	uint8_t double_clicked_on;
	uint8_t motion_state;
	uint8_t motion_state_to_off;
	struct timespec motion_ts;
	uint8_t tld_state;
	double r;
	std::string *label;
	uint8_t midi_note;
	uint8_t midi_channel;
	color color_normal;
	color color_on;
	double get_secs_since_last_on();

};

int color_init(color *c, double r, double g, double b, double a);
color color_copy(color *c);
struct hsva rgb2hsv(color *c);
color hsv2rgb(struct hsva *in);
color color_lighten(color *in, double mag);
#endif
