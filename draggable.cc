#include "draggable.h"

Draggable::Draggable() {
	pos.x = 0.0;
	pos.y = 0.0;
	z = 0;
	this->rotation_radians = 0.0;
	mdown = 0;
}

Draggable::Draggable(double x, double y, uint64_t z) {
	pos.x = x;
	pos.y = y;
	this->z = z;
	this->rotation_radians = 0.0;
	mdown = 0;
}
bool Draggable::render(std::vector<cairo_t *> &contexts) {
	printf("Draggable::render\n");
	return false;
}

void Draggable::set_mdown(double x, double y, uint64_t z) {
	mdown = 1;
	this->z = z;
	mdown_pos.x = x;
	mdown_pos.y = y;
	down_pos.x = pos.x;
	down_pos.y = pos.y;
}

void Draggable::drag(double mouse_x, double mouse_y) {
	if (mdown) {
		pos.x = mouse_x - mdown_pos.x + down_pos.x;
		pos.y = mouse_y - mdown_pos.y + down_pos.y;
	}
}


