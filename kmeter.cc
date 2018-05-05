// ------------------------------------------------------------------------
//
//  Copyright (C) 2008-2011 Fons Adriaensen <fons@linuxaudio.org>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// ------------------------------------------------------------------------

#include <math.h>
#include <string.h>

#include "kmeter.h"
#include "dingle_dots.h"

#define GREY  0.197 / 0.255 * 1.0, 0.203 / 0.255 * 1.0, 0.203 / 0.255   * 1.0

Meter::Meter() {}

void Meter::init(DingleDots *dd, int fsamp, int fsize, float hold, float fall, float x, float y, float w, color c)
{
	// Called by initialisation code.
	// fsamp = sample frequency
	// fsize = period size
	// hold  = peak hold time, seconds
	// fall  = peak fallback rate, dB/s
	this->dingle_dots = dd;
	float t;
	omega = 9.72f / fsamp;                    // ballistic filter coefficient
	t = (float) fsize / fsamp;                    // period time in seconds
	this->hold = (int)(hold / t + 0.5f);            // number of periods to hold peak
	this->fall = powf (10.0f, -0.05f * fall * t);   // per period fallback multiplier
	this->pos.x = x;
	this->pos.y = y;
	this->pos.width = this->pos.height = w;
	this->c = c;
	this->active = 0;
	this->allocated = 1;
}

void Meter::process(float *p, int n)
{
	float  s, t, z1, z2;
	if (flag == 1) {// Display thread has read the rms value.
		rms  = 0;
		flag = 0;
	}
	z1 = this->z1;
	z2 = this->z2;
	t = 0;
	n /= 4;  // Loop is unrolled by 4.
	while (n--) {
		s = *p++;
		s *= s;
		if (t < s) t = s;             // Update digital peak.
		z1 += omega * (s - z1);      // Update first filter.
		s = *p++;
		s *= s;
		if (t < s) t = s;             // Update digital peak.
		z1 += omega * (s - z1);      // Update first filter.
		s = *p++;
		s *= s;
		if (t < s) t = s;             // Update digital peak.
		z1 += omega * (s - z1);      // Update first filter.
		s = *p++;
		s *= s;
		if (t < s) t = s;             // Update digital peak.
		z1 += omega * (s - z1);      // Update first filter.
		z2 += 4 * omega * (z1 - z2); // Update second filter.
	}
	t = sqrtf (t);
	// Save filter state. The added constants avoid denormals.
	this->z1 = z1 + 1e-20f;
	this->z2 = z2 + 1e-20f;
	// Adjust RMS value and update maximum since last read().
	s = sqrtf (2 * z2);
	if (s > rms) rms = s;
	// Digital peak hold and fallback.
	if (t > dpk) {
		// If higher than current value, update and set hold counter.
		dpk = t;
		cnt = hold;
	} else if (cnt) {
		cnt--; // else decrement counter if not zero,
	} else {
		dpk *= fall;     // else let the peak value fall back,
		dpk += 1e-10f;    // and avoid denormals.
	}
}

void Meter::read(float *rms, float *dpk)
{
	// Called by display process approx. 30 times per second.
	//
	// Returns highest _rms value since last call,
	// and current _dpk value.
	*rms = this->rms;
	*dpk = this->dpk;
	flag = 1; // Resets _rms in next process().
}

float lin2dB(float lin) {
	return 20.0f * log10f(lin);
}

float dB2lin(float dB) {
	return pow(10., (dB/20.0));
}

float Meter::mapk20(float v) {
	float ratio = 0.5 * this->pos.width / 450;
	if (v < 0.001) return ratio * (24000 * v);
	v = log (v) / log(10) + 3;
	if (v < 2.0) return ratio * (24.3 + v * (100 + v * 16));
	if (v > 3.0) v = 3.0;
	return ratio * (v * 161.7 - 35.1);
}

bool Meter::render(std::vector<cairo_t *> &contexts)
{
	float rms;
	float dpk;
	float r, r_rms, r_dpk;
	cairo_pattern_t *bg_pat, *fg_pat;
	float x;
	float y;
	this->read(&rms, &dpk);
	r = 0.5 * this->pos.width;
	x = this->pos.x;
	y = this->pos.y;
	r_rms = mapk20(rms);
	r_dpk = mapk20(dpk);
	bg_pat = cairo_pattern_create_radial(x, y, 0, x, y, r);
	float o = 0.5 * this->opacity;
	cairo_pattern_add_color_stop_rgba(bg_pat, 0, 4./256., 4./256., 96./256, o);
	cairo_pattern_add_color_stop_rgba(bg_pat, mapk20(dB2lin(-30))/r, 4./256., 96./256., 4./256, o);
	cairo_pattern_add_color_stop_rgba(bg_pat, mapk20(dB2lin(-20))/r, 96./256., 86./256., 14./256, o);
	cairo_pattern_add_color_stop_rgba(bg_pat, mapk20(1)/r, 96./256., 34./256., 4./256, o);
	fg_pat = cairo_pattern_create_radial(x, y, 0, x, y, r);
	cairo_pattern_add_color_stop_rgba(fg_pat, 0, 21./256., 21./256., 252./256, o);
	cairo_pattern_add_color_stop_rgba(fg_pat, mapk20(dB2lin(-30))/r, 21./256., 252./256., 21./256, o);
	cairo_pattern_add_color_stop_rgba(fg_pat, mapk20(dB2lin(-20))/r, 252./256., 230./256., 72./256, o);
	cairo_pattern_add_color_stop_rgba(fg_pat, mapk20(1)/r, 255./256., 76./256., 0./256, o);
	for (std::vector<cairo_t *>::iterator it = contexts.begin(); it != contexts.end(); ++it) {
		cairo_t *cr = *it;
		cairo_save(cr);
		cairo_translate(cr, x, y);
		cairo_scale(cr, this->scale, this->scale);
		cairo_translate(cr, -x, -y);
		cairo_arc(cr, x, y, r, 0, 2 * M_PI);
		cairo_set_source(cr, bg_pat);
		cairo_fill(cr);
		cairo_arc(cr, x, y, r_rms, 0, 2 * M_PI);
		cairo_set_source(cr, fg_pat);
		cairo_fill(cr);
		cairo_arc(cr, x, y, r_dpk, 0, 2 * M_PI);
		cairo_set_source_rgba(cr, 1, 1, 1, o);
		cairo_stroke(cr);
		if (this->hovered) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
			cairo_arc(cr, x, y, r, 0, 2. * M_PI);
			cairo_fill(cr);
		}
		cairo_restore(cr);
	}
	cairo_pattern_destroy(bg_pat);
	cairo_pattern_destroy(fg_pat);
	return TRUE;
}

int Meter::in(double x_in, double y_in)
{
	if (sqrt(pow((x_in - this->pos.x), 2) + pow(y_in - this->pos.y, 2)) <= 0.5 * this->pos.width * this->scale) {
		return 1;
	} else {
		return 0;
	}
}
