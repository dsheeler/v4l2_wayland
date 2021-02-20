#include <boost/bind.hpp>

#include "sprite.h"
#include "dingle_dots.h"

Sprite::Sprite()
{
	file_path = 0;
	allocated = 0;
	active = 0;
}

void Sprite::free()
{
	this->active = 0;
	this->allocated = 0;
	this->pos.x = this->pos.y = this->pos.width = this->pos.height = 0;
	if (presentation_frame) {
		av_freep(&presentation_frame->data[0]);
		av_frame_free(&presentation_frame);
	}
	av_frame_unref(decoded_frame);
}

Sprite::~Sprite()
{
	free();
}

std::string *Sprite::get_file_path() const
{
	return file_path;
}

void Sprite::create(std::string *name, int z, DingleDots *dd) {
	if (allocated) free();
	this->dingle_dots = dd;
	if (file_path) delete file_path;
	this->z = z;
	file_path = new std::string(name->c_str());
	decoded_frame = av_frame_alloc();
	ff_load_image();
	this->pos.x = 0.5 * (this->dingle_dots->drawing_rect.width - this->pos.width);
	this->pos.y = 0.5 * (this->dingle_dots->drawing_rect.height - this->pos.height);

}

int Sprite::activate() {
	DingleDots *dd = this->dingle_dots;
	return activate_spin(this->scale);
}

bool Sprite::render(std::vector<cairo_t *> &contexts)
{
	cairo_surface_t *tsurf;
	tsurf = cairo_image_surface_create_for_data(
				(unsigned char *)this->presentation_frame->data[0], CAIRO_FORMAT_ARGB32,
				this->pos.width, this->pos.height, 4 * this->pos.width);
	render_surface(contexts, tsurf);
	cairo_surface_destroy(tsurf);
	return TRUE;
}

int Sprite::get_width()
{
	return this->pos.width;
}

int Sprite::get_height()
{
	return this->pos.height;
}

int Sprite::ff_load_image() {
	AVInputFormat *iformat = NULL;
	AVFormatContext *format_ctx = NULL;
	AVCodec *codec;
	AVCodecParameters *codec_parms;
	AVCodecContext *codec_ctx;
	int ret = 0;
	AVPacket pkt;
	struct SwsContext *decoded_to_presentation_ctx;


	av_register_all();

	iformat = av_find_input_format("image2");
	if (iformat == NULL) {
		fprintf(stderr, "Failed to find image format\n");
		return -1;
	}

	if ((ret = avformat_open_input(&format_ctx, file_path->c_str(), iformat, NULL)) < 0) {
		fprintf(stderr,
		"Failed to open input file '%s'\n", file_path->c_str());
		return ret;
	}

	if ((ret = avformat_find_stream_info(format_ctx, NULL)) < 0) {
		fprintf(stderr, "Find stream info failed\n");
		return ret;
	}

	codec_parms = format_ctx->streams[0]->codecpar;
	codec = avcodec_find_decoder(codec_parms->codec_id);
	if (!codec) {
		fprintf(stderr, "Failed to find codec\n");
		ret = AVERROR(EINVAL);
		return ret;
	}

	codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) return -1;

	av_init_packet(&pkt);

	codec_ctx->width = format_ctx->streams[0]->codecpar->width;
	codec_ctx->height = format_ctx->streams[0]->codecpar->height;
	codec_ctx->pix_fmt = (AVPixelFormat)format_ctx->streams[0]->codecpar->format;

	if ((ret = avcodec_open2(codec_ctx, codec, NULL)) < 0) {
		fprintf(stderr, "Failed to open codec\n");
		goto end;
	}

	if (!(decoded_frame = av_frame_alloc()) ) {
		fprintf(stderr, "Failed to alloc frame\n");
		ret = AVERROR(ENOMEM);
		goto end;
	}

	ret = av_read_frame(format_ctx, &pkt);
	if (ret < 0) {
		fprintf(stderr, "Failed to read frame from file\n");
		goto end;
	}

	ret = avcodec_send_packet(codec_ctx,&pkt);
	if (ret < 0) {
		fprintf(stderr, "Error submitting the packet to the decoder\n");
		ret = -1;
		goto end;
	}
	if ((ret = avcodec_receive_frame(codec_ctx, decoded_frame)) >= 0) {
		this->pos.width = decoded_frame->width;
		this->pos.height = decoded_frame->height;
		presentation_frame = av_frame_alloc();
		presentation_frame->format = AV_PIX_FMT_BGRA;
		presentation_frame->width = this->pos.width;
		presentation_frame->height = this->pos.height;
		ret = av_image_alloc(presentation_frame->data, presentation_frame->linesize,
							 presentation_frame->width, presentation_frame->height,
							 (AVPixelFormat)presentation_frame->format, 1);
		if (ret < 0) {
			fprintf(stderr, "Sprite: Could not allocate raw picture buffer\n");
			exit(1);
		}
		decoded_to_presentation_ctx = sws_getContext(decoded_frame->width, decoded_frame->height,
													 (AVPixelFormat) decoded_frame->format, presentation_frame->width,
													 presentation_frame->height, (AVPixelFormat) presentation_frame->format, SWS_BICUBIC, NULL, NULL, NULL);

		sws_scale(decoded_to_presentation_ctx, (const uint8_t * const*)decoded_frame->data,
				  decoded_frame->linesize, 0, decoded_frame->height,
				  (uint8_t * const*)presentation_frame->data,
				  presentation_frame->linesize);
		sws_freeContext(decoded_to_presentation_ctx);
	}
	end:
	av_packet_unref(&pkt);
	//avcodec_close(codec_ctx);					//[sgan]To remove
	avcodec_free_context(&codec_ctx);		//[sgan]To add
	av_frame_free(&decoded_frame);
	if (ret < 0) {
		fprintf(stderr, "Error loading image file '%s'\n", file_path->c_str());
	} else {
		this->allocated = 1;
		this->activate();
	}
	return ret;
}
