#include "vwcolor.h"
#include "math.h"

vwColor::vwColor()
{
	h = 0;
	s = 0;
	v = 0;
	a = 0;
}

vwColor::vwColor(double in_h, double in_s, double in_v, double in_a)
{
	h = in_h;
	s = in_s;
	v = in_v;
	a = in_a;
}


void vwColor::set(color_prop p, double value)
{
	if (p == R || p == G || p == B) {
		double r,g,b;
		hsv2rgb(h, s, v, &r, &g, &b);
		if (p == R) {
			r = value;
		} else if (p == G) {
			g = value;
		} else if (p == B) {
			b = value;
		}
		rgb2hsv(r, g, b, &h, &s, &v);
	} else {
		if (p == H) {
			h = value;
		} else if (p == S) {
			s = value;
		} else if (p == V) {
			v = value;
		} else if (p == A) {
			a = value;
		}
	}
}

void vwColor::set_rgba(double r, double g, double b, double in_a)
{
	rgb2hsv(r, g, b, &h, &s, &v);
	a = in_a;
}

void vwColor::set_hsva(double h, double s, double v, double a)
{
	this->h = h;
	this->v = v;
	this->s = s;
	this->a = a;
}

double vwColor::get(color_prop p)
{
	if (p == R || p == G || p == B) {
		double r,g,b;
		hsv2rgb(h, s, v, &r, &g, &b);
		switch (p) {
			case R:
				return r;
			case G:
				return g;
			case B:
				return b;
		}
	} else {
		switch (p) {
			case H:
				return h;
			case V:
				return v;
			case S:
				return s;
			case A:
				return a;
			default:
				return 1.0;
		}
	}

}

void vwColor::hsv2rgb(double h, double s, double v, double *out_r, double *out_g, double *out_b)
{
	double r, g, b;
	int i = (int)floor(h * 6);
	double f = h * 6 - i;
	double p = v * (1 - s);
	double q = v * (1 - f * s);
	double t = v * (1 - (1 - f) * s);
	switch(i % 6){
		case 0: r = v, g = t, b = p; break;
		case 1: r = q, g = v, b = p; break;
		case 2: r = p, g = v, b = t; break;
		case 3: r = p, g = q, b = v; break;
		case 4: r = t, g = p, b = v; break;
		case 5: r = v, g = p, b = q; break;
	}
	*out_r = r;
	*out_g = g;
	*out_b = b;
	return;
}

void vwColor::rgb2hsv(double r, double g, double b, double *out_h, double *out_s, double *out_v)
{
	double max = fmax(fmax(r, g), b);
	double min = fmin(fmin(r, g), b);
	double h, s, v;
	h = s = v = max;
	double d = max - min;
	s = max == 0 ? 0 : d / max;
	if(max == min){
		h = 0; // achromatic
	} else {
		if (r == max) {
			h = (g - b) / d + (g < b ? 6 : 0);
		} else if (g == max) {
			h = (b - r) / d + 2;
		} else if (b == max) {
			h = (r - g) / d + 4;
		}
		h /= 6;
	}
	*out_h = h;
	*out_s = s;
	*out_v = v;
}
