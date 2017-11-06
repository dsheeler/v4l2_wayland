#include "draggable.h"

void draggable_set_mdown(void *dr, double x, double y, uint64_t z) {
	draggable *d = (draggable *)dr;
	d->mdown = 1;
	d->z = z;
	d->mdown_pos.x = x;
	d->mdown_pos.y = y;
	d->down_pos.x = d->pos.x;
	d->down_pos.y = d->pos.y;
}

void draggable_drag(void *dr, double mouse_x, double mouse_y) {
	draggable *d = (draggable *)dr;
	if (d->mdown) {
		d->pos.x = mouse_x - d->mdown_pos.x + d->down_pos.x;
		d->pos.y = mouse_y - d->mdown_pos.y + d->down_pos.y;
	}
}

void draggable_init(void *dr, double x, double y, uint64_t z) {
	draggable *d = (draggable *)dr;
	d->set_mdown = &draggable_set_mdown;
	d->drag = &draggable_drag;
	d->pos.x = x;
	d->pos.y = y;
	d->z = z;
	d->mdown = 0;
}
