#if !defined (_SPRITE_H)
#define _SPRITE_H (1)

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#ifdef __cplusplus
}
#endif
#include <string>

#include "vwdrawable.h"

class Sprite : public vwDrawable
{
public:
	Sprite();
	Sprite(std::string *file_path);
	~Sprite();
	std::string *get_file_path() const;
	void create(std::string *name, int z, DingleDots *dd);
	bool render(std::vector<cairo_t *> &contexts);
	void free();
	int get_width();
	int get_height();
	//void r(std::vector<cairo_t *> &contexts, cairo_surface_t *tsurf);

	int activate();
protected:
	int ff_load_image();
private:
	std::string *file_path;
	AVFrame *decoded_frame;
	AVFrame *presentation_frame;
};

#endif // SPRITE_H
