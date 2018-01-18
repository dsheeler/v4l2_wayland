#include <gtk/gtk.h>

#include "easer.h"
#include "v4l2_wayland.h"
#include "dingle_dots.h"

Easer::Easer() {}

EasingFuncPtr Easer::easer_type_to_easing_func(Easer_Type type)
{
	EasingFuncPtr func;
	switch (type) {
		case EASER_LINEAR:
			func = &LinearInterpolation;
			break;
		case EASER_QUAD_EASE_IN:
			func = &QuadraticEaseIn;
			break;
		case EASER_QUAD_EASE_OUT:
			func = &QuadraticEaseOut;
			break;
		case EASER_QUAD_EASE_IN_OUT:
			func = &QuadraticEaseInOut;
		case EASER_BOUNCE_EASE_IN:
			func = &BounceEaseIn;
			break;
		case EASER_BOUNCE_EASE_IN_OUT:
			func = &BounceEaseInOut;
			break;
		case EASER_BOUNCE_EASE_OUT:
			func = &BounceEaseOut;
			break;
		default:
			func = 0;
			break;
	}
	return func;
}

void Easer::start(DingleDots *dd, Easer_Type type, double *tvalue, double value_start, double value_finish,
				  double duration_secs)
{
	this->dd = dd;
	this->value = tvalue;
	this->duration_secs = duration_secs;
	this->value_start = value_start;
	this->value_finish = value_finish;
	clock_gettime(CLOCK_MONOTONIC, &this->start_ts);
	this->easing_func = easer_type_to_easing_func(type);
	dd->set_animating(dd->get_animating()+1);
	this->active = TRUE;
}

void Easer::finalize() {
	*this->value = this->value_finish;
	dd->set_animating(dd->get_animating() - 1);
	this->active = FALSE;
}

void Easer::update_value() {
	double ratio_complete = 0.0;
	double delta;
	double easer_value;
	ratio_complete = min(1.0, this->get_ratio_complete());
	easer_value = (this->easing_func)(ratio_complete);
	delta = this->value_finish - this->value_start;
	*this->value = this->value_start + easer_value * delta;
	if (this->done()) {
		finalize();
	}
}

bool Easer::done()
{
	double ratio_complete = get_ratio_complete();
	if (ratio_complete < 1.0) {
		return FALSE;
	} else {
		return TRUE;
	}
}

double Easer::get_ratio_complete() {
	struct timespec now_ts;
	struct timespec diff_ts;
	double ratio_complete;
	double time_passed_secs;
	clock_gettime(CLOCK_MONOTONIC, &now_ts);

	timespec_diff(&this->start_ts, &now_ts, &diff_ts);
	time_passed_secs = timespec_to_seconds(&diff_ts);
	ratio_complete = (time_passed_secs / this->duration_secs);
	return ratio_complete;
}

double Easer::left_secs()
{
	return (1.0 - this->get_ratio_complete()) * this->duration_secs;
}

