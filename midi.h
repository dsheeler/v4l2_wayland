#if !defined (_MIDI_H)
#define _MIDI_H (1)

#include <stdio.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>

extern jack_client_t *client;
extern jack_ringbuffer_t *midi_ring_buf;

struct midi_message {
  jack_nframes_t time;
  int len; /*Length of MIDI message in bytes.*/
  unsigned char data[3];
};

void queue_new_message(int b0, int b1, int b2);

#endif
