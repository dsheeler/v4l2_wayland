#ifndef EASER_H
#define EASER_H

#include <time.h>
#include <vector>
#include "easing.h"
//#include "drawable.h"

typedef enum {
	EASER_LINEAR = 1,
	EASER_QUAD_EASE_IN,
	EASER_QUAD_EASE_OUT,
	EASER_QUAD_EASE_IN_OUT,
	EASER_CUBIC_EASE_IN,
	EASER_CUBIC_EASE_OUT,
	EASER_CUBIC_EASE_IN_OUT,
	EASER_QUARTIC_EASE_IN,
	EASER_QUARTIC_EASE_OUT,
	EASER_QUARTIC_EASE_IN_OUT,
	EASER_QUINTIC_EASE_IN,
	EASER_QUINTIC_EASE_OUT,
	EASER_QUINTIC_EASE_IN_OUT,
	EASER_SINE_EASE_IN,
	EASER_SINE_EASE_OUT,
	EASER_SINE_EASE_IN_OUT,
	EASER_CIRCULAR_EASE_IN,
	EASER_CIRCULAR_EASE_OUT,
	EASER_CIRCULAR_EASE_IN_OUT,
	EASER_EXPONENTIAL_EASE_IN,
	EASER_EXPONENTIAL_EASE_OUT,
	EASER_EXPONENTIAL_EASE_IN_OUT,
	EASER_ELASTIC_EASE_IN,
	EASER_ELASTIC_EASE_OUT,
	EASER_ELASTIC_EASE_IN_OUT,
	EASER_BACK_EASE_IN,
	EASER_BACK_EASE_OUT,
	EASER_BACK_EASE_IN_OUT,
	EASER_BOUNCE_EASE_IN,
	EASER_BOUNCE_EASE_OUT,
	EASER_BOUNCE_EASE_IN_OUT,
	EASER_JIGGLE_EASE,
} Easer_Type;

class DingleDots;
class Drawable;
typedef AHFloat (*EasingFuncPtr)(AHFloat);
class Easer {
public:
	Easer();
	static EasingFuncPtr easer_type_to_easing_func(Easer_Type);
	void initialize(Drawable *target, DingleDots *dd, Easer_Type type, double *value, double value_start, double value_finish, double duration_secs);
	void start();
	void finalize();
	void update_value();
	bool done();
	double get_ratio_complete();
	double left_secs();

	EasingFuncPtr easing_func;
	Drawable *target;
	DingleDots *dd;
	bool active;
	float duration_secs;
	double *value;
	double value_start;
	double value_finish;
	struct timespec start_ts;
	std::vector<Easer *> start_when_finished;
};

#endif // EASER_H
