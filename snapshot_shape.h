#ifndef SNAPSHOT_SHAPE_H
#define SNAPSHOT_SHAPE_H

#include "sound_shape.h"

class SnapshotShape : public SoundShape
{
public:
	SnapshotShape();
	void init(string &label, double x, double y, double r, color c, void *dd);
	int set_on();
	int set_off();
	bool render(std::vector<cairo_t *> &contexts);
	void set_motion_state(uint8_t state);
};

#endif // SNAPSHOT_SHAPE_H
