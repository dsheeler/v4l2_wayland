#include "midi.h"

static void queue_message(DingleDots *dd, struct midi_message *ev) {
	int written;
	if (jack_ringbuffer_write_space(dd->midi_ring_buf) < sizeof(*ev)) {
		fprintf(stderr, "Not enough space in the ringbuffer, NOTE LOST.");
		return;
	}
	written = jack_ringbuffer_write(dd->midi_ring_buf, (char *)ev, sizeof(*ev));
	if (written != sizeof(*ev))
		fprintf(stderr, "jack_ringbuffer_write failed, NOTE LOST.");
}

void midi_queue_new_message(int b0, int b1, int b2, DingleDots *dd) {
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
	queue_message(dd, &ev);
}

void midi_process_output(jack_nframes_t nframes, DingleDots *dd) {
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
	while (jack_ringbuffer_read_space(dd->midi_ring_buf)) {
		read = jack_ringbuffer_peek(dd->midi_ring_buf, (char *)&ev, sizeof(ev));
		if (read != sizeof(ev)) {
			jack_ringbuffer_read_advance(dd->midi_ring_buf, read);
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
		jack_ringbuffer_read_advance(dd->midi_ring_buf, sizeof(ev));
		buffer = jack_midi_event_reserve(port_buffer, t, ev.len);
		memcpy(buffer, ev.data, ev.len);
	}
}

void midi_key_init_by_scale_id(midi_key_t *key, uint8_t base_note,
							   int scaleid) {
	key->base_note = base_note;
	key->scaleid = scaleid;
	memset(key->steps, 0, MAX_SCALE_LENGTH * sizeof(uint8_t));
	switch(scaleid) {
		case MAJOR:
			key->num_steps = 8;
			key->steps[0] = 0;
			key->steps[1] = 2;
			key->steps[2] = 4;
			key->steps[3] = 5;
			key->steps[4] = 7;
			key->steps[5] = 9;
			key->steps[6] = 11;
			key->steps[7] = 12;
			break;
		case MINOR:
			key->num_steps = 8;
			key->steps[0] = 0;
			key->steps[1] = 2;
			key->steps[2] = 3;
			key->steps[3] = 5;
			key->steps[4] = 7;
			key->steps[5] = 8;
			key->steps[6] = 10;
			key->steps[7] = 12;
			break;
		case CHROMATIC:
			key->num_steps = 12;
			key->steps[0] = 0;
			key->steps[1] = 1;
			key->steps[2] = 2;
			key->steps[3] = 3;
			key->steps[4] = 4;
			key->steps[5] = 5;
			key->steps[6] = 6;
			key->steps[7] = 7;
			key->steps[8] = 8;
			key->steps[9] = 9;
			key->steps[10] = 10;
			key->steps[11] = 11;
			key->steps[12] = 12;
			break;
		case SINGLE:
			key->num_steps = 1;
			key->steps[0] = 0;
			break;
		default:
			key->num_steps = 0;
	}
}

float midi_to_freq(int midi_note) {
	return 440.0 * pow(2.0, (midi_note - 69.0) / 12.0);
}

int midi_scale_text_to_id(char *name) {
	if (strcmp(name, "Major") == 0) {
		return MAJOR;
	} else if (strcmp(name, "Minor") == 0) {
		return MINOR;
	} else if (strcmp(name, "Chromatic") == 0) {
		return CHROMATIC;
	} else if (strcmp(name, "Single") == 0) {
		return SINGLE;
	}
	return -1;
}

char *midi_scale_id_to_text(int scaleid) {
	switch (scaleid) {
		case MAJOR:
			return "Major";
		case MINOR:
			return "Minor";
		case CHROMATIC:
			return "Chromatic";
		case SINGLE:
			return "Single";
		default:
			return "None";
	}
}

void midi_note_to_octave_name(uint8_t midi_note, char *text) {
	char *note_names[] = {"C", "C#", "D", "D#", "E",
						  "F", "F#", "G", "G#", "A", "A#", "B"};
	int note_names_idx, note_octave_num;
	note_octave_num = midi_note / 12 - 1;
	note_names_idx = midi_note % 12;
	sprintf(text, "%s%d", note_names[note_names_idx], note_octave_num);
}
