#include "sprite.h"

Sprite::Sprite()
{
	file_path = 0;
	allocated = 0;
	active = 0;
}

Sprite::Sprite(std::string *file_path)
{
	this->create(file_path, 0);
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

void Sprite::create(std::string *name, int z) {
	if (allocated) free();
	if (file_path) delete file_path;
	this->z = z;
	file_path = new std::string(name->c_str());
	decoded_frame = av_frame_alloc();
	ff_load_image();
}



bool Sprite::render(std::vector<cairo_t *> &contexts)
{
	cairo_surface_t *tsurf;
	tsurf = cairo_image_surface_create_for_data(
				(unsigned char *)this->presentation_frame->data[0], CAIRO_FORMAT_ARGB32,
				this->pos.width, this->pos.height, 4 * this->pos.width);
	render_surface(contexts, tsurf);
	cairo_surface_destroy(tsurf);
}

int Sprite::ff_load_image() {
	AVInputFormat *iformat = NULL;
	AVFormatContext *format_ctx = NULL;
	AVCodec *codec;
	AVCodecContext *codec_ctx;
	int frame_decoded, ret = 0;
	AVPacket pkt;
	struct SwsContext *decoded_to_presentation_ctx;

	av_init_packet(&pkt);

	av_register_all();

	iformat = av_find_input_format("image2");
	if ((ret = avformat_open_input(&format_ctx, file_path->c_str(), iformat, NULL)) < 0) {
		fprintf(stderr,
		"Failed to open input file '%s'\n", file_path);
		return ret;
	}

	if ((ret = avformat_find_stream_info(format_ctx, NULL)) < 0) {
		fprintf(stderr, "Find stream info failed\n");
		return ret;
	}

	codec_ctx = format_ctx->streams[0]->codec;				//[sgan]To remove
	codec = avcodec_find_decoder(codec_ctx->codec_id);	//[sgan]To modify
	if (!codec) {
		fprintf(stderr, "Failed to find codec\n");
		ret = AVERROR(EINVAL);
		goto end;
	}

	codec_ctx = avcodec_alloc_context3(codec);	//[sgan]To add
	if (!codec_ctx) return -1;									//[sgan]To add


	codec_ctx->width=format_ctx->streams[0]->codec->width;			//[sgan]To add
	codec_ctx->height=format_ctx->streams[0]->codec->height;		//[sgan]To add
	codec_ctx->pix_fmt=format_ctx->streams[0]->codec->pix_fmt;	//[sgan]To add

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

	ret = avcodec_decode_video2(codec_ctx, decoded_frame, &frame_decoded, &pkt);
	if (ret < 0 || !frame_decoded) {
		fprintf(stderr, "Failed to decode image from file\n");
		if (ret >= 0)
		ret = -1;
		goto end;
	}
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
	end:
	av_free_packet(&pkt);
	//avcodec_close(codec_ctx);					//[sgan]To remove
	avcodec_free_context(&codec_ctx);		//[sgan]To add
	avformat_close_input(&format_ctx);
	sws_freeContext(decoded_to_presentation_ctx);
	if (ret < 0)
		fprintf(stderr, "Error loading image file '%s'\n", file_path->c_str());
	else
		this->allocated = 1;
		this->active = 1;
	return ret;
}
