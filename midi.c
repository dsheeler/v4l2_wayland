#include "midi.h"

void queue_message(struct midi_message *ev) {
  int written;
  if (jack_ringbuffer_write_space(midi_ring_buf) < sizeof(*ev)) {
    fprintf(stderr, "Not enough space in the ringbuffer, NOTE LOST.");
    return;
  }
  written = jack_ringbuffer_write(midi_ring_buf, (char *)ev, sizeof(*ev));
  if (written != sizeof(*ev))
    fprintf(stderr, "jack_ringbuffer_write failed, NOTE LOST.");
}

void queue_new_message(int b0, int b1, int b2) {
  struct midi_message ev;
  if (b1 == -1) {
    ev.len = 1;
    ev.data[0] = b0;
  } else if (b2 == -1) {
    ev.len = 2;
    ev.data[0] = b0;
    ev.data[1] = b1;
  } else {
    ev.len = 3;
    ev.data[0] = b0;
    ev.data[1] = b1;
    ev.data[2] = b2;
  }
  ev.time = jack_frame_time(client);
  queue_message(&ev);
}



