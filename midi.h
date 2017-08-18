#if !defined (_MIDI_H)
#define _MIDI_H (1)

#include <stdio.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>
#include "v4l2_wayland.h"
#include "dingle_dots.h"

extern jack_client_t *client;
extern jack_ringbuffer_t *midi_ring_buf;

struct midi_message {
  jack_nframes_t time;
  int len; /*Length of MIDI message in bytes.*/
  unsigned char data[3];
};

void queue_new_message(int b0, int b1, int b2, dingle_dots_t *dd);
void process_midi_output(jack_nframes_t nframes, dingle_dots_t *dd);
float midi_to_freq(int midi_note);
int midi_scale_init(midi_scale_t *scale, uint8_t *notes, uint8_t nb_notes);
int midi_key_init(midi_key_t *key, uint8_t base_note, midi_scale_t *scale);

#endif
