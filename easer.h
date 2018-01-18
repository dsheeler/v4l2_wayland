#ifndef EASER_H
#define EASER_H

#include <time.h>
#include "easing.h"

typedef enum {
	EASER_LINEAR = 1,
	EASER_QUAD_EASE_IN,
	EASER_QUAD_EASE_OUT,
	EASER_QUAD_EASE_IN_OUT,
	EASER_BOUNCE_EASE_IN,
	EASER_BOUNCE_EASE_OUT,
	EASER_BOUNCE_EASE_IN_OUT
} Easer_Type;

class DingleDots;

typedef AHFloat (*EasingFuncPtr)(AHFloat);
class Easer {
public:
	Easer();
	static EasingFuncPtr easer_type_to_easing_func(Easer_Type);
	void start(DingleDots *dd, Easer_Type type, double *value, double value_start, double value_finish, double duration_secs);
	void finalize();
	void update_value();
	bool done();
	double get_ratio_complete();
	double left_secs();

	EasingFuncPtr easing_func;
	DingleDots *dd;
	bool active;
	float duration_secs;
	double *value;
	double value_start;
	double value_finish;
	struct timespec start_ts;
};

#endif // EASER_H
