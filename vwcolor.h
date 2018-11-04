#ifndef VWCOLOR_H
#define VWCOLOR_H

#include "easable.h"

typedef enum {
	H = 1,
	S,
	V,
	R,
	G,
	B,
	A
} color_prop;

class vwColor : public Easable
{
public:
	vwColor();
	vwColor(vwDrawable *d);
	vwColor(double h, double s, double v, double a);
	void set(color_prop p, double value);
	void set_r(double value) { set(R, value); }
	void set_g(double value) { set(G, value); }
	void set_b(double value) { set(B, value); }
	void set_h(double value) { set(H, value); }
	void set_s(double value) { set(S, value); }
	void set_v(double value) { set(B, value); }
	void set_a(double value) { set(A, value); }

	void set_rgba(double r, double g, double b, double a);
	void set_hsva(double h, double s, double v, double a);
	double get(color_prop p);
private:
	static void hsv2rgb(double h, double s, double v, double *r, double *g, double *b);
	static void rgb2hsv(double r, double g, double b, double *h, double *s, double *v);
	double h, s, v, a;
};

#endif // VWCOLOR_H
