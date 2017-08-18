#include "midi.h"

static void queue_message(struct midi_message *ev) {
  int written;
  if (jack_ringbuffer_write_space(midi_ring_buf) < sizeof(*ev)) {
    fprintf(stderr, "Not enough space in the ringbuffer, NOTE LOST.");
    return;
  }
  written = jack_ringbuffer_write(midi_ring_buf, (char *)ev, sizeof(*ev));
  if (written != sizeof(*ev))
    fprintf(stderr, "jack_ringbuffer_write failed, NOTE LOST.");
}

void queue_new_message(int b0, int b1, int b2, dingle_dots_t *dd) {
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
  ev.time = jack_frame_time(dd->client);
  queue_message(&ev);
}

void process_midi_output(jack_nframes_t nframes, dingle_dots_t *dd) {
  int read, t;
  unsigned char *buffer;
  void *port_buffer;
  jack_nframes_t last_frame_time;
  struct midi_message ev;
  last_frame_time = jack_last_frame_time(dd->client);
  port_buffer = jack_port_get_buffer(dd->midi_port, nframes);
  if (port_buffer == NULL) {
    return;
  }
  jack_midi_clear_buffer(port_buffer);
  while (jack_ringbuffer_read_space(midi_ring_buf)) {
    read = jack_ringbuffer_peek(midi_ring_buf, (char *)&ev, sizeof(ev));
    if (read != sizeof(ev)) {
      jack_ringbuffer_read_advance(midi_ring_buf, read);
      continue;
    }
    t = ev.time + nframes - last_frame_time;
    /* If computed time is too much into
     * the future, we'll need
     *       to send it later. */
    if (t >= (int)nframes)
      break;
    /* If computed time is < 0, we
     * missed a cycle because of xrun.
     * */
    if (t < 0)
      t = 0;
    jack_ringbuffer_read_advance(midi_ring_buf, sizeof(ev));
    buffer = jack_midi_event_reserve(port_buffer, t, ev.len);
    memcpy(buffer, ev.data, ev.len);
  }
}

int midi_scale_init(midi_scale_t *scale, uint8_t *notes, uint8_t nb_notes) {
  scale->notes = notes;
  scale->nb_notes = nb_notes;
  return 0;
}

int midi_key_init(midi_key_t *key, uint8_t base_note, midi_scale_t *scale) {
  key->base_note = base_note;
  key->scale = scale;
  return 0;
}

float midi_to_freq(int midi_note) {
	return 440.0 * pow(2.0, (midi_note - 69.0) / 12.0);
}


