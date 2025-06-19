#include "hex.h"
#include "dingle_dots.h"
#include "vwcolor.h"
#include "text.h"

#include <cairo/cairo.h>

#define BLUE 0.0, 0.152 / 0.255 * 1.0, 0.212 / 0.255 * 1.0
#define BLUE_TRANS 0.0, 0.152 / 0.255 * 1.0, 0.212 / 0.255 * 1.0, 0.255
#define BLUE_TRANS2 0.0, 0.152 / 0.255 * 1.0, 0.212 / 0.255 * 1.0, 0.144 / 0.255 * 1.0
#define BLUE_TRANS3 0.0, 0.122 / 0.255 * 1.0, 0.112 / 0.255 * 1.0, 0.144 / 0.255 * 1.0
#define GREEN  0.001 / 0.255 * 1.0, 0.187 / 0.255 * 1.0, 0.0
#define LGREEN  0.001 / 0.255 * 1.0, 0.187 / 0.255 * 1.0, 0.0, 0.044 / 0.255 * 1.0
#define WHITE 0.222 / 0.255 * 1.0, 0.232 / 0.255 * 1.0, 0.233 / 0.255 * 1.0
#define WHITE_TRANS 0.222 / 0.255 * 1.0, 0.232 / 0.255 * 1.0, 0.233 / 0.255 * 1.0, 0.555
#define ORANGE  0.255 / 0.255 * 1.0, 0.080 / 0.255 * 1.0, 0.0
#define GREY  0.197 / 0.255 * 1.0, 0.203 / 0.255 * 1.0, 0.203 / 0.255   * 1.0
#define GREY2  0.037 / 0.255 * 1.0, 0.037 / 0.255 * 1.0, 0.038 / 0.255   * 1.0
#define BGCOLOR  0.033 / 0.255 * 1.0, 0.033 / 0.255 * 1.0, 0.033 / 0.255   * 1.0
#define GREY3  0.103 / 0.255 * 1.0, 0.103 / 0.255 * 1.0, 0.124 / 0.255   * 1.0
#define BGCOLOR_TRANS  0.033 / 0.255 * 1.0, 0.033 / 0.255 * 1.0, 0.033 / 0.255 * 1.0, 0.144 / 0.255 * 1.0
#define BGCOLOR_CLR 0.0, 0.0, 0.0, 1.0

Hex::Hex()
{
	active = 0;
	allocated = 0;
}

void Hex::create(double x, double y, double w, vwColor c, DingleDots *dd)
{
	this->pos.x = x;
	this->pos.y = y;
    this->pos.width = w;
    this->pos.height = w * sqrt(3) / 2; // height of hexagon based on width
    this->rotation_radians = 0.0;
	this->z = dd->next_z++;
	this->dingle_dots = dd;
	this->c = c;
    Easer *e = new Easer(true);
    e->initialize(this->dingle_dots, this, EASER_LINEAR, std::bind(&vwDrawable::set_rotation,
                                                                               this, std::placeholders::_1), 0.0, 360, 4.0);
    e->start();
	active = 1;
	allocated = 1;
}

void Hex::free()
{
}

bool Hex::render(std::vector<cairo_t *> &contexts) {

  
    int r1;
    float scale;
  
    int x = this->pos.x + 0.5 * this->pos.width;
    int y = this->pos.y + 0.5 * this->pos.height;

    float w = this->pos.width;
    float r = this->c.get(R);
    float g = this->c.get(G);
    float b = this->c.get(B);
    float rotation = this->get_rotation();
    float opacity = this->c.get(A);
    
    scale = 2.5;
  
    r1 = ((w)/2 * sqrt(3));
    for (std::vector<cairo_t *>::iterator it = contexts.begin(); it != contexts.end(); ++it) {
		cairo_t *cr = *it;
        cairo_save(cr);

        cairo_set_line_width(cr, 1);
        cairo_set_source_rgba(cr, r, g, b, opacity * this->get_opacity());
        
        cairo_translate(cr, x, y);
        cairo_scale(cr, this->get_scale(), this->get_scale());

        cairo_rotate(cr, rotation * (M_PI/180.0));
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
    
        cairo_fill(cr);
    
        cairo_restore(cr);
    
   
    }
    return true;
  }
  
