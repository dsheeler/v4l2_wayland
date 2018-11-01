#include "text.h"
#include "dingle_dots.h"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

Text::Text()
{
	active = 0;
	allocated = 0;
}

void Text::init(char *text, char *font, uint font_size, DingleDots *dd)
{
	PangoLayout *layout;
	PangoFontDescription *desc;
	int width, height;
	char lfont[32];
	this->text = new std::string(text);
	this->font = new std::string(font);
	this->pos.x = 0;
	this->pos.y = 0;
	this->z = dd->next_z++;
	this->dingle_dots = dd;
	this->font_size = font_size;
	sprintf(lfont, "%s %d", this->font->c_str(), this->font_size);
	cairo_surface_t *tsurf = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
														dd->drawing_rect.width,
														dd->drawing_rect.height);
	cairo_t *cr = cairo_create(tsurf);
	layout = pango_cairo_create_layout(cr);
	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_text(layout, this->text->c_str(), -1);
	desc = pango_font_description_from_string(lfont);
	pango_layout_set_font_description(layout, desc);
	pango_layout_get_size(layout, &width, &height);
	pos.width = width / PANGO_SCALE;
	pos.height = height / PANGO_SCALE;
	cairo_surface_destroy(tsurf);
	cairo_destroy(cr);
	pango_font_description_free(desc);
	g_object_unref(layout);
	allocated = 1;
}

bool Text::render(std::vector<cairo_t *> &contexts)
{
	PangoLayout *layout;
	PangoFontDescription *desc;
	char lfont[32];
	sprintf(lfont, "%s %d", this->font->c_str(), font_size);
	for (std::vector<cairo_t *>::iterator it = contexts.begin(); it != contexts.end(); ++it) {
		cairo_t *cr = *it;
		layout = pango_cairo_create_layout(cr);
		pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
		pango_layout_set_text(layout, this->text->c_str(), -1);
		desc = pango_font_description_from_string(lfont);
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		cairo_save(cr);
		cairo_translate(cr, this->pos.x, this->pos.y);
		cairo_translate(cr, 0.5 * this->pos.width, 0.5 * this->pos.height);
		cairo_scale(cr, this->scale, this->scale);
		cairo_rotate(cr, this->get_rotation());
		cairo_translate(cr, -0.5 * this->pos.width, -0.5 * this->pos.height);
		cairo_set_source_rgba(cr, 1., 1., 1., this->get_opacity());
		pango_cairo_show_layout(cr, layout);
		if (this->hovered) {
			render_hovered(cr);
		}
		cairo_restore(cr);
		g_object_unref(layout);
	}
}
