#if !defined (_KMETER_H)
#define _KMETER_H (1)

#include "v4l2_wayland.h"
#include "drawable.h"

class Meter : public Drawable {
public:
	Meter();
	void init(DingleDots *dd, int fsamp, int fsize, float hold, float fall,
				 float x, float y, float w, color c);
	void process(float *p, int n);
	void read(float *rms, float *dpk);
	bool render(std::vector<cairo_t *> &contexts);
	int in(double x_in, double y_in);
	float mapk20(float v);
	private:
	float  z1;          // filter state
	float  z2;          // filter state
	float  rms;         // max rms value since last read()
	float  dpk;         // current digital peak value
	int    cnt;	       // digital peak hold counter
	int    flag;        // flag set by read(), resets _rms
	int    hold;
	float  fall;
	float  omega;
	struct color  c;
	float  x;
	float  y;
	float  w;
};

#endif
