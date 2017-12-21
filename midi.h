#if !defined (_MIDI_H)
#define _MIDI_H (1)

#include <stdio.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>
#include "v4l2_wayland.h"
#include "dingle_dots.h"

#define MAX_SCALE_LENGTH 64

typedef struct midi_key_t midi_key_t;
class DingleDots;

typedef enum {
	MAJOR = 0,
	MINOR,
	CHROMATIC,
	SINGLE
} scales;

struct midi_message {
	jack_nframes_t time;
	int len; /*Bytes.*/
	unsigned char data[3];
};

struct midi_key_t {
	uint8_t base_note;
	int scaleid;
	int steps[MAX_SCALE_LENGTH];
	int num_steps;
};

void midi_queue_new_message(int b0, int b1, int b2, DingleDots *dd);
void midi_process_output(jack_nframes_t nframes, DingleDots *dd);
float midi_to_freq(int midi_note);
void midi_key_init_by_scale_id(midi_key_t *key, uint8_t base_note, int scaleid);
int midi_scale_text_to_id(char *name);
char *midi_scale_id_to_text(int scaleid);
void midi_note_to_octave_name(uint8_t midi_note, char *text);
#endif
