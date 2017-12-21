#if !defined (_SOUND_SHAPE_H)
#define _SOUND_SHAPE_H (1)

typedef struct color color;

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <math.h>
#include <string.h>
#include <string>
#include <stdint.h>
#include <gdk/gdk.h>
//#include "dingle_dots.h"
#include "draggable.h"
#include "v4l2_wayland.h"

#define NCHAR 32
#define MAX_NSOUND_SHAPES 256

#ifdef __cplusplus
using namespace std;
#endif


typedef struct color {
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

class DingleDots;
class SoundShape : public Draggable {
public:
	SoundShape();
	void init(string &label, uint8_t midi_note, uint8_t midi_channel,
			  double x, double y, double r, color *c, DingleDots *dd);
	bool render(std::vector<cairo_t *> &contexts);
	void render_label(cairo_t *cr);
	int activate();
	int deactivate();
	int in(double x, double y);
	int set_on();
	int set_off();
	void tick();
	int is_on();
	void clear_state();
	void set_motion_state(uint8_t state);
	//	private:
	DingleDots *dd;
	uint8_t active;
	uint8_t on;
	uint8_t double_clicked_on;
	uint8_t selected;
	uint8_t hovered;
	uint8_t motion_state;
	uint8_t motion_state_to_off;
	struct timespec motion_ts;
	uint8_t tld_state;
	GdkPoint selected_pos;
	double r;
	string *label;
	uint8_t midi_note;
	uint8_t midi_channel;
	color normal;
	color playing;

};

int color_init(color *c, double r, double g, double b, double a);
color color_copy(color *c);
struct hsva rgb2hsv(color *c);
color hsv2rgb(struct hsva *in);
color color_lighten(color *in, double mag);

#endif
