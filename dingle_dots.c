#include <jack/jack.h>

#include "dingle_dots.h"
#include "midi.h"

int dingle_dots_init(dingle_dots_t *dd, int width, int height) {
	memset(dd, 0, sizeof(dingle_dots_t));
	dd->width = width;
	dd->height = height;
	dd->doing_tld = 0;
	dd->doing_motion = 0;
	dd->motion_threshold = 0.001;
	memset(dd->sound_shapes, 0, MAX_NSOUND_SHAPES * sizeof(sound_shape));
	return 0;
}

void dingle_dots_add_scale(dingle_dots_t *dd, midi_key_t *key) {
	int i;
  double x_delta;
	color c;
	srand(time(NULL));
	struct hsva h;
	h.h = (double) rand() / RAND_MAX;
	h.v = 0.45;
	h.s = 1.0;
	h.a = 0.5;
	c = hsv2rgb(&h);
	x_delta = 1. / (key->num_steps + 1);
	for (i = 0; i < key->num_steps; i++) {
		char key_name[NCHAR];
		char base_name[NCHAR];
		char *scale;
		midi_note_to_octave_name(key->base_note, base_name);
		scale = midi_scale_id_to_text(key->scaleid);
		sprintf(key_name, "%s %s", base_name, scale);
		dingle_dots_add_note(dd, key_name, i + 1,
		 key->base_note + key->steps[i],
		 x_delta * (i + 1) * dd->width, dd->height / 2.,
		 dd->width/32, &c);
	}
}

int dingle_dots_add_note(dingle_dots_t *dd, char *scale_name,
 int scale_num, int midi_note, double x, double y, double r, color *c) {
  char label[NCHAR], snum[NCHAR], octave_name[NCHAR];
  int i;
  double freq;
	memset(label, '\0', NCHAR * sizeof(char));
	memset(snum, '\0', NCHAR * sizeof(char));
	memset(octave_name, '\0', NCHAR * sizeof(char));
	freq = midi_to_freq(midi_note);
	if (scale_num > 0) {
		sprintf(snum, "%s", scale_name);
	}
	midi_note_to_octave_name(midi_note, octave_name);
	sprintf(label, "%.2f\n%s\n%d\n%s", freq, snum, scale_num, octave_name);
	for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
		if (dd->sound_shapes[i].active) continue;
		sound_shape_init(&dd->sound_shapes[i], label, midi_note,
		 x, y, r, c, dd);
		sound_shape_activate(&dd->sound_shapes[i]);
		return 0;
	}
	return -1;
}

