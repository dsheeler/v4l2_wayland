#include <jack/jack.h>

#include "dingle_dots.h"
#include "midi.h"

int dingle_dots_init(dingle_dots_t *dd, midi_key_t *keys, uint8_t nb_keys,
 int width, int height) {
  int i;
  int j;
  double x_delta;
  color colors[2];
  color_init(&colors[0], 30./255., 100./255., 80./255., 0.5);
  color_init(&colors[1], 0., 30./255., 80./255., 0.5);
	memset(dd, 0, sizeof(dingle_dots_t));
	dd->width = width;
	dd->height = height;
	dd->doing_tld = 0;
	dd->doing_motion = 0;
	dd->motion_threshold = 0.001;
	memset(dd->sound_shapes, 0, MAX_NSOUND_SHAPES * sizeof(sound_shape));
  for (i = 0; i < nb_keys; i++) {
    x_delta = 1. / (keys[i].scale->nb_notes + 1);
    for (j = 0; j < keys[i].scale->nb_notes; j++) {
			dingle_dots_add_note(dd, j + 1,
			 keys[i].base_note + keys[i].scale->notes[j],
			 x_delta * (j + 1) * dd->width, (i+1) * dd->height / 4.,
			 dd->width/(keys[i].scale->nb_notes*4), &colors[i%2]);
		}
	}
	return 0;
}

int dingle_dots_add_note(dingle_dots_t *dd, int scale_num, int midi_note,
 double x, double y, double r, color *c) {
  char label[NCHAR], snum[NCHAR];
  int i;
  double freq;
  char *note_names[] = {"C", "C#", "D", "D#", "E",
   "F", "F#", "G", "G#", "A", "A#", "B"};
  int note_names_idx, note_octave_num;
	note_octave_num = (midi_note - 12) / 12;
	note_names_idx = (midi_note - 12) % 12;
	memset(label, '\0', NCHAR * sizeof(char));
	memset(snum, '\0', NCHAR * sizeof(char));
	freq = midi_to_freq(midi_note);
	if (scale_num > 0) {
		sprintf(snum, "%d", scale_num);
	}
	sprintf(label, "%s\n%.2f\n%s%d", snum, freq, note_names[note_names_idx],
 	 note_octave_num);
	for (i = 0; i < MAX_NSOUND_SHAPES; i++) {
		if (dd->sound_shapes[i].active) continue;
		sound_shape_init(&dd->sound_shapes[i], label, midi_note,
		 x, y, r, c, dd);
		sound_shape_activate(&dd->sound_shapes[i]);
		return 0;
	}
	return -1;
}

