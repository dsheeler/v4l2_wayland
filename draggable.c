#include "draggable.h"

void draggable_set_mdown(void *this, double x, double y, uint64_t z) {
	draggable *d = (draggable *)this;
	d->mdown = 1;
	d->z = z;
	d->mdown_pos.x = x;
	d->mdown_pos.y = y;
	d->down_pos.x = d->pos.x;
	d->down_pos.y = d->pos.y;
}

void draggable_drag(void *this, double mouse_x, double mouse_y) {
	draggable *d = (draggable *)this;
	if (d->mdown) {
		d->pos.x = mouse_x - d->mdown_pos.x + d->down_pos.x;
		d->pos.y = mouse_y - d->mdown_pos.y + d->down_pos.y;
	}
}

void draggable_init(void *this, double x, double y, uint64_t z) {
	draggable *d = (draggable *)this;
	d->set_mdown = &draggable_set_mdown;
	d->drag = &draggable_drag;
	d->pos.x = x;
	d->pos.y = y;
	d->z = z;
	d->mdown = 0;
}
