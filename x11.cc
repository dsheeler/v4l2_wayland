#include "x11.h"
#include "dingle_dots.h"

#define WIN_STRING_DIV "\r\n"

X11::X11()
{
	XInitThreads();
	using_window = false;
	display = NULL;
}

std::list<Window> X11::get_top_level_windows()
{
	std::list<Window> res;
	Display *disp = XOpenDisplay(NULL);

	Atom netClList = XInternAtom(disp, "_NET_CLIENT_LIST", true);
	Atom actualType;
	int format;
	unsigned long num, bytes;
	Window* data = 0;


	for (int i = 0; i < ScreenCount(disp); ++i) {
		Window rootWin = RootWindow(disp, i);

		int status = XGetWindowProperty(
						 disp,
						 rootWin,
						 netClList,
						 0L,
						 ~0L,
						 false,
						 AnyPropertyType,
						 &actualType,
						 &format,
						 &num,
						 &bytes,
						 (uint8_t**)&data);

		if (status != Success) {
			continue;
		}

		for (unsigned long i = 0; i < num; ++i)
			res.push_back(data[i]);

		XFree(data);
	}

	return res;
}

int getRootWindowScreen(Window root)
{
	XWindowAttributes attr;
	Display *disp = XOpenDisplay(NULL);

	if (!XGetWindowAttributes(disp, root, &attr))
		return DefaultScreen(disp);

	return XScreenNumberOfScreen(attr.screen);
}

std::string getWindowAtom(Window win, const char *atom)
{
	Display *disp = XOpenDisplay(NULL);
	Atom netWmName = XInternAtom(disp, atom, false);
	int n;
	char **list = 0;
	XTextProperty tp;
	std::string res = "unknown";

	XGetTextProperty(disp, win, &tp, netWmName);

	if (!tp.nitems)
		XGetWMName(disp, win, &tp);

	if (!tp.nitems)
		return "error";

	if (tp.encoding == XA_STRING) {
		res = (char*)tp.value;
	} else {
		int ret = XmbTextPropertyToTextList(disp, &tp, &list,
				&n);

		if (ret >= Success && n > 0 && *list) {
			res = *list;
			XFreeStringList(list);
		}
	}

	XFree(tp.value);

	return res;
}

std::string getWindowClass(Window win)
	{
		return getWindowAtom(win, "WM_CLASS");
	}

std::string X11::get_window_name(Window win)
	{
		return getWindowAtom(win, "_NET_WM_NAME");
	}

Window X11::get_window_from_string(std::string wstr)
{
	if (wstr == "") {
		return X11::get_top_level_windows().front();
	}
	for (Window cwin: X11::get_top_level_windows()) {
		std::string cwinname = X11::get_window_name(cwin);
		std::string ccls = getWindowClass(cwin);

		if (wstr == cwinname)
			return cwin;
	}
	return -1;
}

void X11::get_display_dimensions(int *w, int*h) {
	XWindowAttributes DOSBoxWindowAttributes;
	Display *display = XOpenDisplay(nullptr);
	Window rootWindow = RootWindow(display, DefaultScreen(display));
	XGetWindowAttributes(display, rootWindow, &DOSBoxWindowAttributes);
	*w = DOSBoxWindowAttributes.width;
	*h = DOSBoxWindowAttributes.height;
    XCloseDisplay(display);
}

void *X11::thread(void *arg) {
	X11 *x = (X11 *)arg;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);
	int rc = pthread_setname_np(x->thread_id, "vw_x11");
	if (rc != 0) {
		errno = rc;
		perror("pthread_setname_np");
	}
	x->event_loop();
	return nullptr;
}

void X11::event_loop() {
	Display *new_disp = XOpenDisplay(nullptr);
	XVisualInfo vinfo;
	XShmSegmentInfo *shminfo;
	int ret;
	shminfo = (XShmSegmentInfo *)malloc(sizeof(*shminfo));
	ret = XMatchVisualInfo(new_disp, DefaultScreen(new_disp), 32, TrueColor, &vinfo);
	if (ret == 0) {
		printf("X11::event_loop: no visual found: abort\n");
		return;
	}
	this->shm_image = XShmCreateImage(new_disp, vinfo.visual, 32,
									  ZPixmap, nullptr, shminfo, this->xpos.width,
									  this->xpos.height);
	shminfo->shmid = shmget(IPC_PRIVATE, this->shm_image->bytes_per_line *
							 this->shm_image->height,
							 IPC_CREAT|0777);
	shminfo->shmaddr = this->shm_image->data = (char *)shmat(shminfo->shmid, nullptr, 0);
	shminfo->readOnly = False;
	ret = XShmAttach(new_disp, shminfo);
	if (ret == 0) {
		printf("X11::event_loop: couldn't attach shared memory segment\n");
		return;
	}
	this->surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
											this->xpos.width,
											this->xpos.height);
	this->allocated = 1;
	XSelectInput(new_disp, this->window, StructureNotifyMask);
	while (!this->done) {
		XEvent e;
		XNextEvent (new_disp, & e);
		pthread_mutex_lock(&this->lock);
		if (e.type == ConfigureNotify) {
			this->allocated	= 0;
			this->pos.width = this->xpos.width = e.xconfigure.width;
			this->pos.height = this->xpos.height = e.xconfigure.height;
			this->xpos.x = e.xconfigure.x;
			this->xpos.y = e.xconfigure.y;
			XDestroyImage(this->shm_image);
			this->shm_image = XShmCreateImage(new_disp, vinfo.visual, 32,
											  ZPixmap, nullptr, shminfo, this->xpos.width,
											  this->xpos.height);
			shminfo->shmid = shmget(IPC_PRIVATE, this->shm_image->bytes_per_line *
									 this->shm_image->height,
									 IPC_CREAT|0777);
			shminfo->shmaddr = this->shm_image->data = (char *)shmat(shminfo->shmid, nullptr, 0);
			shminfo->readOnly = False;
			ret = XShmAttach(new_disp, shminfo);
			cairo_surface_destroy(this->surf);
			this->surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
													this->xpos.width,
													this->xpos.height);
			this->allocated = 1;
		}
		pthread_mutex_unlock(&this->lock);
	}
}

void X11::init(DingleDots *dd, int x, int y, int w, int h) {
	this->display = XOpenDisplay(NULL);
	this->rootWindow = RootWindow(display, DefaultScreen(display));
	this->window = this->rootWindow;
	this->xpos.width = w;
	this->xpos.height = h;
	this->xpos.x = x;
	this->xpos.y = y;
	this->z = dd->next_z++;
	this->pos.width = w;
	this->pos.height = h;
	this->pos.x = 0;
	this->pos.y = 0;
	this->done = false;
	this->dingle_dots = dd;
	this->hovered = 0;
	pthread_create(&this->thread_id, nullptr, X11::thread, this);
}

void X11::init_window(DingleDots *dd, Window win) {
	int w, h;
	XWindowAttributes win_att;
	this->window = win;
	this->display = XOpenDisplay(NULL);
	this->rootWindow = RootWindow(display, DefaultScreen(display));
	XGetWindowAttributes(this->display, this->window, &win_att);
	w = win_att.width;
	h = win_att.height;
	this->xpos.width = w;
	this->xpos.height = h;
	this->xpos.x = win_att.x;
	this->xpos.y = win_att.y;
	this->z = dd->next_z++;
	this->pos.width = w;
	this->pos.height = h;
	this->pos.x = 0;
	this->pos.y = 0;
	this->done = false;
	this->dingle_dots = dd;
	this->hovered = 0;
	this->using_window = true;
	pthread_create(&this->thread_id, nullptr, X11::thread, this);
}

void X11::uninit()
{
	this->allocated = 0;
	this->active = 0;
	this->hovered = 0;
	XDestroyImage(this->shm_image);
	cairo_surface_destroy(this->surf);
	if (this->thread_id) pthread_cancel(this->thread_id);
}

/*This funciton is stolen verbatim from simple screen recorder.*/
static void X11ImageDrawCursor(Display* dpy, XImage* image, int recording_area_x, int recording_area_y) {

	// check the image format
	unsigned int pixel_bytes, r_offset, g_offset, b_offset;
	if(image->bits_per_pixel == 24 && image->red_mask == 0xff0000 && image->green_mask == 0x00ff00 && image->blue_mask == 0x0000ff) {
		pixel_bytes = 3;
		r_offset = 2; g_offset = 1; b_offset = 0;
	} else if(image->bits_per_pixel == 24 && image->red_mask == 0x0000ff && image->green_mask == 0x00ff00 && image->blue_mask == 0xff0000) {
		pixel_bytes = 3;
		r_offset = 0; g_offset = 1; b_offset = 2;
	} else if(image->bits_per_pixel == 32 && image->red_mask == 0xff0000 && image->green_mask == 0x00ff00 && image->blue_mask == 0x0000ff) {
		pixel_bytes = 4;
		r_offset = 2; g_offset = 1; b_offset = 0;
	} else if(image->bits_per_pixel == 32 && image->red_mask == 0x0000ff && image->green_mask == 0x00ff00 && image->blue_mask == 0xff0000) {
		pixel_bytes = 4;
		r_offset = 0; g_offset = 1; b_offset = 2;
	} else if(image->bits_per_pixel == 32 && image->red_mask == 0xff000000 && image->green_mask == 0x00ff0000 && image->blue_mask == 0x0000ff00) {
		pixel_bytes = 4;
		r_offset = 3; g_offset = 2; b_offset = 1;
	} else if(image->bits_per_pixel == 32 && image->red_mask == 0x0000ff00 && image->green_mask == 0x00ff0000 && image->blue_mask == 0xff000000) {
		pixel_bytes = 4;
		r_offset = 1; g_offset = 2; b_offset = 3;
	} else {
		return;
	}

	// get the cursor
	XFixesCursorImage *xcim = XFixesGetCursorImage(dpy);
	if(xcim == NULL)
		return;

	// calculate the position of the cursor
	int x = xcim->x - xcim->xhot - recording_area_x;
	int y = xcim->y - xcim->yhot - recording_area_y;

	// calculate the part of the cursor that's visible
	int cursor_left = std::max(0, -x), cursor_right = std::min((int) xcim->width, image->width - x);
	int cursor_top = std::max(0, -y), cursor_bottom = std::min((int) xcim->height, image->height - y);

	// draw the cursor
	// XFixesCursorImage uses 'long' instead of 'int' to store the cursor images, which is a bit weird since
	// 'long' is 64-bit on 64-bit systems and only 32 bits are actually used. The image uses premultiplied alpha.
	for(int j = cursor_top; j < cursor_bottom; ++j) {
		unsigned long *cursor_row = xcim->pixels + xcim->width * j;
		uint8_t *image_row = (uint8_t*) image->data + image->bytes_per_line * (y + j);
		for(int i = cursor_left; i < cursor_right; ++i) {
			unsigned long cursor_pixel = cursor_row[i];
			uint8_t *image_pixel = image_row + pixel_bytes * (x + i);
			int cursor_a = (uint8_t) (cursor_pixel >> 24);
			int cursor_r = (uint8_t) (cursor_pixel >> 16);
			int cursor_g = (uint8_t) (cursor_pixel >> 8);
			int cursor_b = (uint8_t) (cursor_pixel >> 0);
			if(cursor_a == 255) {
				image_pixel[r_offset] = cursor_r;
				image_pixel[g_offset] = cursor_g;
				image_pixel[b_offset] = cursor_b;
			} else {
				image_pixel[r_offset] = (image_pixel[r_offset] * (255 - cursor_a) + 127) / 255 + cursor_r;
				image_pixel[g_offset] = (image_pixel[g_offset] * (255 - cursor_a) + 127) / 255 + cursor_g;
				image_pixel[b_offset] = (image_pixel[b_offset] * (255 - cursor_a) + 127) / 255 + cursor_b;
			}
		}
	}

	// free the cursor
	XFree(xcim);

}

bool X11::render(std::vector<cairo_t *> &contexts) {
	if (!this->active || !this->allocated) return TRUE;
	GTimer *timer = g_timer_new();
	gdouble sec;
	gulong usec;
	XColor colors;
	XImage *image;
	unsigned long red_mask;
	unsigned long green_mask;
	unsigned long blue_mask;
	if (pthread_mutex_trylock(&this->lock) == 0) {
		if (this->using_window) {
			image = this->shm_image;
			XShmGetImage(this->display, this->window, image, 0, 0, AllPlanes);
		} else {
			image = this->shm_image;
			XShmGetImage(this->display, this->window, image, this->xpos.x, this->xpos.y, AllPlanes);
		}
		if (image) {
			cairo_surface_flush(surf);
			unsigned char *data = cairo_image_surface_get_data(surf);
			X11ImageDrawCursor(this->display, image, this->xpos.x, this->xpos.y);
			red_mask = image->red_mask;
			green_mask = image->green_mask;
			blue_mask = image->blue_mask;
			for (int i = 0; i < this->xpos.height; ++i) {
				for (int j = 0; j < this->xpos.width; ++j) {
					colors.pixel = XGetPixel(image, j, i);
					data[4*i*((int)this->xpos.width) + 4*j] = (colors.pixel & blue_mask);
					data[4*i*((int)this->xpos.width) + 4*j + 1] = (colors.pixel & green_mask)>>8;
					data[4*i*((int)this->xpos.width) + 4*j + 2] = (colors.pixel & red_mask)>>16;
				}
			}
		}
		render_surface(contexts, surf);
		pthread_mutex_unlock(&this->lock);
	}
	sec = g_timer_elapsed(timer, &usec);
	printf("X11::render took %02f seconds\n", sec);
	return TRUE;
}

void X11::deactivate_action()
{
	this->uninit();
}
