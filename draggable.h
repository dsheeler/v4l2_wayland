#if !defined (_DRAGGABLE_H)
#define _DRAGGABLE_H (1)

#include "gtk/gtk.h"
#include <stdint.h>

typedef struct draggable {
	void (*set_mdown)(void* dr, double x, double y, uint64_t z);
	void (*drag)(void *dr, double mouse_x, double mouse_y);
	uint64_t z;
	int mdown;
	GdkRectangle pos;
	GdkRectangle mdown_pos;
	GdkRectangle down_pos;
} draggable;

void draggable_set_mdown(void *dr, double x, double y, uint64_t z);
void draggable_drag(void *dr, double mouse_x, double mouse_y);
void draggable_init(void *dr, double x, double y, uint64_t z);

#endif
