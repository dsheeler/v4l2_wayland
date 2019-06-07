#include "text.h"
#include "dingle_dots.h"
#include "vwcolor.h"
#include "text.h"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

Text::Text()
{
	active = 0;
	allocated = 0;
}

void Text::create(char *text, char *font, DingleDots *dd)
{
	PangoLayout *layout;
	PangoFontDescription *desc;
	int width, height;
	char lfont[32];
	this->text = (text);
	this->font = (font);
	this->pos.x = 0;
	this->pos.y = 0;
	this->z = dd->next_z++;
	this->dingle_dots = dd;
	this->color.set_rgba(0., 0., 0., 0.);
	sprintf(lfont, "%s", this->font.c_str());
	cairo_surface_t *tsurf = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
														dd->drawing_rect.width,
														dd->drawing_rect.height);
	cairo_t *cr = cairo_create(tsurf);
	layout = pango_cairo_create_layout(cr);
	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_text(layout, this->text.c_str(), -1);
	desc = pango_font_description_from_string(lfont);
	pango_layout_set_font_description(layout, desc);
	pango_layout_get_size(layout, &width, &height);
	pos.width = width / PANGO_SCALE;
	pos.height = height / PANGO_SCALE;
	cairo_surface_destroy(tsurf);
	cairo_destroy(cr);
	pango_font_description_free(desc);
	g_object_unref(layout);
	active = 0;
	allocated = 1;
}

void Text::free()
{
}

bool Text::render(std::vector<cairo_t *> &contexts)
{
	PangoLayout *layout;
	PangoFontDescription *desc;
	char lfont[32];
	sprintf(lfont, "%s", this->font.c_str());
	for (std::vector<cairo_t *>::iterator it = contexts.begin(); it != contexts.end(); ++it) {
		cairo_t *cr = *it;
		layout = pango_cairo_create_layout(cr);
		pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
		pango_layout_set_text(layout, this->text.c_str(), -1);
		desc = pango_font_description_from_string(lfont);
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		cairo_save(cr);
		cairo_translate(cr, this->pos.x, this->pos.y);
		cairo_translate(cr, 0.5 * this->pos.width, 0.5 * this->pos.height);
		cairo_scale(cr, this->scale, this->scale);
		cairo_rotate(cr, this->get_rotation());
		cairo_translate(cr, -0.5 * this->pos.width, -0.5 * this->pos.height);
		cairo_set_source_rgba(cr, color.get(R), color.get(G), color.get(B), color.get(A) * this->get_opacity());
		pango_cairo_show_layout(cr, layout);
		if (this->hovered) {
			render_hovered(cr);
		}
		cairo_restore(cr);
		g_object_unref(layout);
	}
}

void Text::set_color_hsva(double h, double s, double v, double a) {
	this->color.set_hsva(h, s, v, a);
	this->dingle_dots->queue_draw();
}

void Text::set_color_rgba(double r, double g, double b, double a)
{
	this->color.set_rgba(r, g, b, a);
	this->dingle_dots->queue_draw();
}

void Text::set_color(color_prop p, double v)
{
	this->color.set(p, v);
	this->dingle_dots->queue_draw();
}
