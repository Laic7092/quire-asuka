// BookPage on Windows: a child window of the packages' own class, showing
// the engine's EQP1 raw bitmap with GDI (StretchDIBits, so the page fills
// the view) and the highlights composited over it in the same pixels. The
// files are read here, not through Asuka: the pixels never touch the Asuka
// runtime.
//
// The window is asuka_win_window's, as native packages make theirs
// (examples/media/segmented/desktop/segmented_windows.c): its messages go
// to handle below, and its memory goes with it at WM_NCDESTROY.
#include "asuka_windows.h"
#include "bookpage.asuka.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// How long a still finger (or a still button) takes to start a selection,
/// how far it may move and still be a tap, and how soon after a selection's
/// lift a click is that selection's own, not a page turn (milliseconds).
#define HOLD_MS 500
#define TAP_SLOP 5.0
#define SELECTION_OWN_MS 800

typedef struct {
	bookpage_BookPage base;
	bookpage_BookPage_events events;
	HWND hwnd;
	/// The page's pixels as GDI's DIB wants them (BGRA, rows top down), its
	/// size in device pixels, and where those pixels came from.
	unsigned char *page;
	uint32_t width, height;
	/// The page with the highlights over it; NULL while there are none.
	unsigned char *shown;
	/// The app's highlights, in the order it gave them.
	bookpage_list_Highlight marks;
	/// Device pixels to a point, as the engine rendered the page.
	double scale;
	/// The app says it has a selection up: any drag adjusts it.
	bool has_selection;
	bool destroyed;
	/// Where the pointer went down, and where it is now, in page points.
	double press_x, press_y, last_x, last_y;
	/// Whether a button (or a finger) is down, and whether a drag started.
	bool down, dragging;
	/// When a selection last ended (GetTickCount64), 0 before the first.
	uint64_t selection_ended;
} view;

static uint32_t le32(const unsigned char *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/// The page's pixels, from the file's 12-byte header (EQP1, width, height)
/// and its RGBA8 rows, as a DIB's BGRA. NULL when the file isn't a page.
static unsigned char *read_page(const char *path, uint32_t *out_width, uint32_t *out_height) {
	if (!path || !*path) return NULL;
	wchar_t *name = asuka_wide(path);
	FILE *file = name ? _wfopen(name, L"rb") : NULL;
	free(name);
	if (!file) return NULL;
	unsigned char header[12];
	if (fread(header, 1, sizeof header, file) != sizeof header || memcmp(header, "EQP1", 4) != 0) {
		fclose(file);
		return NULL;
	}
	uint32_t width = le32(header + 4), height = le32(header + 8);
	// A page is thousands of pixels a side; this keeps a broken header from
	// asking for gigabytes.
	if (width == 0 || height == 0 || width > 16384 || height > 16384) {
		fclose(file);
		return NULL;
	}
	size_t size = (size_t)width * height * 4;
	unsigned char *pixels = malloc(size);
	if (!pixels || fread(pixels, 1, size, file) != size) {
		free(pixels);
		fclose(file);
		return NULL;
	}
	fclose(file);
	for (size_t i = 0; i + 3 < size; i += 4) {
		unsigned char r = pixels[i];
		pixels[i] = pixels[i + 2];
		pixels[i + 2] = r;
	}
	*out_width = width;
	*out_height = height;
	return pixels;
}

/// The highlights as one layer over the page: the page's pixels with each
/// rectangle blended in (the highlight's alpha over it, source over), at
/// the bitmap's own size, so one stretch to the view lines the two up.
static void composite(view *v) {
	free(v->shown);
	v->shown = NULL;
	if (!v->page || !v->marks.len || v->width == 0 || v->height == 0) return;
	size_t size = (size_t)v->width * v->height * 4;
	unsigned char *out = malloc(size);
	if (!out) return;
	memcpy(out, v->page, size);
	double s = v->scale > 0 ? v->scale : 1.0;
	for (size_t i = 0; i < v->marks.len; i++) {
		const bookpage_Highlight *h = &v->marks.data[i];
		if (h->w <= 0 || h->h <= 0) continue;
		long x0 = lround(h->x * s), y0 = lround(h->y * s);
		long x1 = lround((h->x + h->w) * s), y1 = lround((h->y + h->h) * s);
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > (long)v->width) x1 = v->width;
		if (y1 > (long)v->height) y1 = v->height;
		if (x1 <= x0 || y1 <= y0) continue;
		uint32_t c = (uint32_t)h->color;
		unsigned char r = (c >> 24) & 0xff, g = (c >> 16) & 0xff, b = (c >> 8) & 0xff, a = c & 0xff;
		if (a == 0) continue;
		for (long y = y0; y < y1; y++) {
			unsigned char *row = out + ((size_t)y * v->width + x0) * 4;
			for (long x = x0; x < x1; x++) {
				row[0] = (unsigned char)((b * a + row[0] * (255 - a) + 127) / 255);
				row[1] = (unsigned char)((g * a + row[1] * (255 - a) + 127) / 255);
				row[2] = (unsigned char)((r * a + row[2] * (255 - a) + 127) / 255);
				row[3] = 255;
				row += 4;
			}
		}
	}
	v->shown = out;
}

static void repaint(view *v) {
	if (v->hwnd && !v->destroyed) InvalidateRect(v->hwnd, NULL, FALSE);
}

/// The page and the highlights that were over it: the view's pixels, not
/// the view itself (the window owns that until WM_NCDESTROY).
static void release_pixels(view *v) {
	free(v->page);
	free(v->shown);
	v->page = NULL;
	v->shown = NULL;
	v->width = v->height = 0;
	bookpage_list_Highlight_free(&v->marks);
}

/// A client point as a page point: the page is stretched to fill the view,
/// so its point space is the bitmap's size at the engine's scale.
static void to_points(view *v, LPARAM lp, double *x, double *y) {
	RECT r;
	GetClientRect(v->hwnd, &r);
	double pw = v->width && v->scale > 0 ? (double)v->width / v->scale : (double)r.right;
	double ph = v->height && v->scale > 0 ? (double)v->height / v->scale : (double)r.bottom;
	int cx = (int)(short)LOWORD(lp), cy = (int)(short)HIWORD(lp);
	*x = r.right > 0 ? (double)cx * pw / (double)r.right : 0;
	*y = r.bottom > 0 ? (double)cy * ph / (double)r.bottom : 0;
}

static bool handle(void *ctx, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT *result) {
	(void)wp;
	view *v = ctx;
	if (v->destroyed && msg != WM_NCDESTROY) return false;
	switch (msg) {
	case WM_ERASEBKGND:
		// WM_PAINT covers every pixel, the page or the paper.
		return true;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT r;
		GetClientRect(hwnd, &r);
		if (v->page && v->width && v->height && r.right > 0 && r.bottom > 0) {
			BITMAPINFO bmi;
			memset(&bmi, 0, sizeof bmi);
			bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
			bmi.bmiHeader.biWidth = (LONG)v->width;
			bmi.bmiHeader.biHeight = -(LONG)v->height;
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			bmi.bmiHeader.biCompression = BI_RGB;
			// The page is a photograph of text, not a stretch of one: HALFTONE
			// (with its brush origin) is the smooth interpolation.
			SetStretchBltMode(hdc, HALFTONE);
			SetBrushOrgEx(hdc, 0, 0, NULL);
			StretchDIBits(hdc, 0, 0, r.right, r.bottom, 0, 0, (int)v->width, (int)v->height, v->shown ? v->shown : v->page,
			              &bmi, DIB_RGB_COLORS, SRCCOPY);
		} else {
			FillRect(hdc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
		}
		EndPaint(hwnd, &ps);
		*result = 0;
		return true;
	}
	case WM_SIZE:
		InvalidateRect(hwnd, NULL, FALSE);
		return false;
	case WM_LBUTTONDOWN:
		if (!v->page) return false;
		SetCapture(hwnd);
		to_points(v, lp, &v->press_x, &v->press_y);
		v->last_x = v->press_x;
		v->last_y = v->press_y;
		v->down = true;
		v->dragging = false;
		// A finger held still starts a selection; a mouse drags it out.
		SetTimer(hwnd, 1, HOLD_MS, NULL);
		return true;
	case WM_MOUSEMOVE:
		if (!v->down) return false;
		to_points(v, lp, &v->last_x, &v->last_y);
		if (v->dragging) {
			bookpage_BookPage_drag(v->events, v->last_x, v->last_y);
			return true;
		}
		{
			double dx = v->last_x - v->press_x, dy = v->last_y - v->press_y;
			// A few points of jitter are still a tap, unless a selection is
			// up: then the drag adjusts it.
			if (!v->has_selection && dx * dx + dy * dy < TAP_SLOP * TAP_SLOP) return true;
		}
		KillTimer(hwnd, 1);
		v->dragging = true;
		bookpage_BookPage_hold(v->events, v->press_x, v->press_y);
		bookpage_BookPage_drag(v->events, v->last_x, v->last_y);
		return true;
	case WM_TIMER:
		KillTimer(hwnd, 1);
		if (!v->down || v->dragging) return true;
		v->dragging = true;
		bookpage_BookPage_hold(v->events, v->press_x, v->press_y);
		return true;
	case WM_LBUTTONUP:
		KillTimer(hwnd, 1);
		// Let go before releasing: the WM_CAPTURECHANGED that follows sees the
		// lift already handled.
		v->down = false;
		ReleaseCapture();
		if (v->dragging) {
			v->dragging = false;
			v->selection_ended = GetTickCount64();
			bookpage_BookPage_release(v->events);
			return true;
		}
		// The click that comes with a selection's lift is not a page turn.
		if (v->selection_ended && GetTickCount64() - v->selection_ended < SELECTION_OWN_MS) return true;
		to_points(v, lp, &v->press_x, &v->press_y);
		bookpage_BookPage_press(v->events, v->press_x, v->press_y);
		return true;
	case WM_CAPTURECHANGED:
		// The button went up where we can't see it: a drag ends here, a tap
		// simply doesn't happen.
		if (!v->down) return false;
		v->down = false;
		KillTimer(hwnd, 1);
		if (v->dragging) {
			v->dragging = false;
			v->selection_ended = GetTickCount64();
			bookpage_BookPage_release(v->events);
		}
		return false;
	case WM_NCDESTROY:
		KillTimer(hwnd, 1);
		release_pixels(v);
		free(v);
		return false;
	}
	return false;
}

static void set_path(bookpage_BookPage *self, const char *path) {
	view *v = (view *)self;
	if (v->destroyed) return;
	uint32_t width = 0, height = 0;
	unsigned char *pixels = read_page(path, &width, &height);
	if (!pixels) return;
	free(v->page);
	v->page = pixels;
	v->width = width;
	v->height = height;
	composite(v);
	repaint(v);
}

static void set_highlights(bookpage_BookPage *self, bookpage_list_Highlight value) {
	view *v = (view *)self;
	if (v->destroyed) return;
	bookpage_list_Highlight_free(&v->marks);
	v->marks.len = value.len;
	v->marks.data = value.len ? calloc(value.len, sizeof *value.data) : NULL;
	if (value.len && !v->marks.data) v->marks.len = 0;
	for (size_t i = 0; i < v->marks.len; i++) v->marks.data[i] = value.data[i];
	composite(v);
	repaint(v);
}

static void set_selecting(bookpage_BookPage *self, bool value) {
	((view *)self)->has_selection = value;
}

static void set_scale(bookpage_BookPage *self, double value) {
	view *v = (view *)self;
	v->scale = value;
	composite(v);
	repaint(v);
}

static void destroy(bookpage_BookPage *self) {
	view *v = (view *)self;
	if (v->destroyed) return;
	v->destroyed = true;
	// The host destroys the window after this; WM_NCDESTROY frees the view.
	if (v->hwnd) KillTimer(v->hwnd, 1);
	release_pixels(v);
}

static const bookpage_BookPage_vtable vtable = {
	.destroy = destroy,
	.set_path = set_path,
	.set_highlights = set_highlights,
	.set_selecting = set_selecting,
	.set_scale = set_scale,
};

bookpage_BookPage *bookpage_make_BookPage(bookpage_BookPage_events events) {
	view *v = calloc(1, sizeof *v);
	if (!v) return NULL;
	v->base.vt = &vtable;
	v->events = events;
	v->scale = 1;
	HWND hwnd = asuka_win_window(0, L"BookPage", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, (HWND)events.parent, handle, v);
	if (!hwnd) {
		free(v);
		return NULL;
	}
	v->hwnd = hwnd;
	v->base.widget = hwnd;
	return &v->base;
}
