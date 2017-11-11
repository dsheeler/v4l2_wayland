#if !defined (_SOUND_SHAPE_H)
#define _SOUND_SHAPE_H (1)

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <math.h>
#include <string.h>
#include <string>
#include <stdint.h>
#include <gdk/gdk.h>
#include "dingle_dots.h"
#include "draggable.h"
#include "v4l2_wayland.h"

#define NCHAR 32
#define MAX_NSOUND_SHAPES 256

#ifdef __cplusplus
using namespace std;
#endif
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

class SoundShape : Draggable {
	public:
		SoundShape(string &label, uint8_t midi_note, uint8_t midi_channel,
				double x, double y, double r, color *c, dingle_dots_t *dd);
		int render(cairo_t *cr);
		int activate();
		int deactivate();
		int in(double x, double y);
		int set_on();
		int set_off();
		void tick();
		int is_on(); 
		void set_motion_state(uint8_t state);
	private:
		dingle_dots_t *dd;
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
		string label[NCHAR];
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
