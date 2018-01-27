#include <gtk/gtk.h>

#include "easer.h"
#include "v4l2_wayland.h"
#include "dingle_dots.h"
#include "drawable.h"

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
			break;
		case EASER_CUBIC_EASE_IN:
			func = & CubicEaseIn;
			break;
		case EASER_CUBIC_EASE_OUT:
			func = &CubicEaseOut;
			break;
		case EASER_CUBIC_EASE_IN_OUT:
			func = &CubicEaseInOut;
			break;
		case EASER_QUARTIC_EASE_IN:
			func = &QuarticEaseIn;
			break;
		case EASER_QUARTIC_EASE_OUT:
			func = &QuarticEaseOut;
			break;
		case EASER_QUARTIC_EASE_IN_OUT:
			func = &QuarticEaseInOut;
			break;
		case EASER_QUINTIC_EASE_IN:
			func = &QuinticEaseIn;
			break;
		case EASER_QUINTIC_EASE_OUT:
			func = &QuinticEaseOut;
			break;
		case EASER_QUINTIC_EASE_IN_OUT:
			func = &QuinticEaseInOut;
			break;
		case EASER_SINE_EASE_IN:
			func = &SineEaseIn;
			break;
		case EASER_SINE_EASE_OUT:
			func = &SineEaseOut;
			break;
		case EASER_SINE_EASE_IN_OUT:
			func = &SineEaseInOut;
			break;
		case EASER_CIRCULAR_EASE_IN:
			func = &CircularEaseIn;
			break;
		case EASER_CIRCULAR_EASE_OUT:
			func = &CircularEaseOut;
			break;
		case EASER_CIRCULAR_EASE_IN_OUT:
			func = &CircularEaseInOut;
			break;
		case EASER_EXPONENTIAL_EASE_IN:
			func = &ExponentialEaseIn;
			break;
		case EASER_EXPONENTIAL_EASE_OUT:
			func = &ExponentialEaseOut;
			break;
		case EASER_EXPONENTIAL_EASE_IN_OUT:
			func = &ExponentialEaseInOut;
			break;
		case EASER_BACK_EASE_IN:
			func = &BackEaseIn;
			break;
		case EASER_BACK_EASE_OUT:
			func = &BackEaseOut;
			break;
		case EASER_BACK_EASE_IN_OUT:
			func = &BackEaseInOut;
			break;
		case EASER_ELASTIC_EASE_IN:
			func = &ElasticEaseIn;
			break;
		case EASER_ELASTIC_EASE_OUT:
			func = &ElasticEaseOut;
			break;
		case EASER_ELASTIC_EASE_IN_OUT:
			func = &ElasticEaseInOut;
			break;
		case EASER_BOUNCE_EASE_IN:
			func = &BounceEaseIn;
			break;
		case EASER_BOUNCE_EASE_OUT:
			func = &BounceEaseOut;
			break;
		case EASER_BOUNCE_EASE_IN_OUT:
			func = &BounceEaseInOut;
			break;
		default:
			func = 0;
			break;
	}
	return func;
}

void Easer::initialize(Drawable *target, Easer_Type type, boost::function<void(double)> functor,
					   double value_start, double value_finish,
					   double duration_secs)
{
	this->active = FALSE;
	this->target = target;
	this->value = 0;
	this->setter = functor;
	this->duration_secs = duration_secs;
	this->value_start = value_start;
	this->value_finish = value_finish;
	this->easing_func = easer_type_to_easing_func(type);
}

void Easer::start() {
	target->easers.push_back(this);
	this->active = TRUE;
	clock_gettime(CLOCK_MONOTONIC, &this->start_ts);
}

void Easer::finalize() {
	this->setter(this->value_finish);
	for (std::vector<Easer *>::iterator it = this->finsh_easers.begin();
		 it != this->finsh_easers.end(); ++it) {
		Easer *e = *it;
		e->start();
	}
	for (std::vector<boost::function <void()>>::iterator it = this->finish_actions.begin();
		 it != this->finish_actions.end(); ++it) {
		auto action = *it;
		action();
	}
	this->active = FALSE;
}

void Easer::update_value() {
	double ratio_complete = 0.0;
	double delta;
	double easer_value;
	ratio_complete = min(1.0, this->get_ratio_complete());
	easer_value = (this->easing_func)(ratio_complete);
	delta = this->value_finish - this->value_start;
	this->setter(this->value_start + easer_value * delta);
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

double Easer::time_left_secs()
{
	return (1.0 - this->get_ratio_complete()) * this->duration_secs;
}

void Easer::add_finish_easer(Easer *e)
{
	this->finsh_easers.push_back(e);
}

void Easer::add_finish_action(boost::function<void()> action)
{
	this->finish_actions.push_back(action);
}

