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
#define GREY  0.197 / 0.255 * 1.0, 0.203 / 0.255 * 1.0, 0.203 / 0.255   * 1.0

void kmeter_process(kmeter *km, float *p, int n) {
	float  s, t, z1, z2;
	if (km->flag == 1) {// Display thread has read the rms value.
		km->rms  = 0;
		km->flag = 0;
	}
	z1 = km->z1;
	z2 = km->z2;
	t = 0;
	n /= 4;  // Loop is unrolled by 4.
	while (n--) {
		s = *p++;
		s *= s;
		if (t < s) t = s;             // Update digital peak.
		z1 += km->omega * (s - z1);      // Update first filter.
		s = *p++;
		s *= s;
		if (t < s) t = s;             // Update digital peak.
		z1 += km->omega * (s - z1);      // Update first filter.
		s = *p++;
		s *= s;
		if (t < s) t = s;             // Update digital peak.
		z1 += km->omega * (s - z1);      // Update first filter.
		s = *p++;
		s *= s;
		if (t < s) t = s;             // Update digital peak.
		z1 += km->omega * (s - z1);      // Update first filter.
		z2 += 4 * km->omega * (z1 - z2); // Update second filter.
	}
	t = sqrtf (t);
	// Save filter state. The added constants avoid denormals.
	km->z1 = z1 + 1e-20f;
	km->z2 = z2 + 1e-20f;
	// Adjust RMS value and update maximum since last read().
	s = sqrtf (2 * z2);
	if (s > km->rms) km->rms = s;
	// Digital peak hold and fallback.
	if (t > km->dpk) {
		// If higher than current value, update and set hold counter.
		km->dpk = t;
		km->cnt = km->hold;
	} else if (km->cnt) {
		km->cnt--; // else decrement counter if not zero,
	} else {
		km->dpk *= km->fall;     // else let the peak value fall back,
		km->dpk += 1e-10f;    // and avoid denormals.
	}
}

void kmeter_read(kmeter *km, float *rms, float *dpk) {
	// Called by display process approx. 30 times per second.
	//
	// Returns highest _rms value since last call,
	// and current _dpk value.
	*rms = km->rms;
	*dpk = km->dpk;
	km->flag = 1; // Resets _rms in next process().
}

void kmeter_init(kmeter *km, int fsamp, int fsize, float hold, float fall,
				 float x, float y, float w, color c) {
	// Called by initialisation code.
	// fsamp = sample frequency
	// fsize = period size
	// hold  = peak hold time, seconds
	// fall  = peak fallback rate, dB/s
	float t;
	memset(km, 0, sizeof(*km));
	km->omega = 9.72f / fsamp;                    // ballistic filter coefficient
	t = (float) fsize / fsamp;                    // period time in seconds
	km->hold = (int)(hold / t + 0.5f);            // number of periods to hold peak
	km->fall = powf (10.0f, -0.05f * fall * t);   // per period fallback multiplier
	km->x = x;
	km->y = y;
	km->w = w;
	km->c = c;
}

void kmeter_render(kmeter *km, cairo_t *cr, float opacity) {
	float rms;
	float dpk;
	int r1;
	float scale;
	static float hexrot = 0;
	cairo_pattern_t *pat;
	float x;
	float y;
	float w;
	color *c;
	kmeter_read(km, &rms, &dpk);
	x = km->x;
	y = km->y;
	w = dpk * km->w;
	c = &km->c;
	cairo_save(cr);
	cairo_set_line_width(cr, 1);
	cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a);
	scale = 2.5;
	r1 = ((w)/2 * sqrt(3));
	cairo_translate(cr, x, y);
	cairo_rotate(cr, hexrot * (M_PI/180.0));
	cairo_translate(cr, -(w/2), -r1);
	cairo_move_to(cr, 0, 0);
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_rel_line_to(cr, w, 0);
	hexrot += 1.5;
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_save(cr);
	cairo_set_line_width(cr, 1.5);
	cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
	cairo_set_source_rgba(cr, GREY, opacity);
	cairo_translate(cr, x, y);
	cairo_rotate(cr, hexrot * (M_PI/180.0));
	cairo_translate(cr, -((w * scale)/2), -r1 * scale);
	cairo_scale(cr, scale, scale);
	cairo_move_to(cr, 0, 0);
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_rel_line_to(cr, w, 0);
	cairo_rotate(cr, 60 * (M_PI/180.0));
	cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
	pat = cairo_pattern_create_radial (w/2, r1, 3, w/2, r1, r1*scale);
	cairo_pattern_add_color_stop_rgba (pat, 0, c->r, c->g, c->b, opacity);
	cairo_pattern_add_color_stop_rgba (pat, 0.4, 0, 0, 0, 0);
	cairo_set_source(cr, pat);
	cairo_fill(cr);
	cairo_pattern_destroy (pat);
	/*cairo_save(cr);
  cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a);
  cairo_translate(cr, x, y);
  cairo_arc(cr, 0, 0, ss->r, 0, 2 * M_PI);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.5*c->r, 0.5*c->g, 0.5*c->b, 0.75);
  cairo_stroke(cr);
  cairo_restore(cr);*/
	cairo_restore(cr);
}
