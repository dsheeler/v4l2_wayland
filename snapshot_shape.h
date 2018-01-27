#ifndef SNAPSHOT_SHAPE_H
#define SNAPSHOT_SHAPE_H

#include "sound_shape.h"
#include "easer.h"

class SnapshotShape : public SoundShape
{
public:
	SnapshotShape();
	void init(char *label, double x, double y, double r, color c, void *dd);
	int set_on();
	int set_off();
	bool render(std::vector<cairo_t *> &contexts);
	double countdown_seconds_left();
	void set_motion_state(uint8_t state);
	double radius_on;
	Easer countdown_radius_easer;
	double get_radius_on() const;
	void set_radius_on(double value);
};

#endif // SNAPSHOT_SHAPE_H
