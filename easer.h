#ifndef EASER_H
#define EASER_H

#include <time.h>
#include <vector>
#include "easing.h"
#include <boost/function.hpp>
//#include "vwcolor.h"
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
class vwDrawable;
class vwColor;
class Easable;
typedef AHFloat (*EasingFuncPtr)(AHFloat);
class Easer {
public:
	Easer();
	static EasingFuncPtr easer_type_to_easing_func(Easer_Type);
	void start();
	void finalize();
	void update_value();
	bool done();
	double get_ratio_complete();
	double time_left_secs();
	void add_finish_easer(Easer *e);
	void add_finish_action(boost::function<void ()> action);
	EasingFuncPtr easing_func;

	Easable *target;
	boost::function<void(double)> setter;
	DingleDots *dd;
	bool active;
	float duration_secs;
	double *value;
	double value_start;
	double value_finish;
	struct timespec start_ts;
	std::vector<Easer *> finsh_easers;
	std::vector<boost::function<void ()>> finish_actions;
	void initialize(DingleDots *dd, Easable *target, Easer_Type type, boost::function<void (double)>, double value_start, double value_finish, double duration_secs);
};

#endif // EASER_H
