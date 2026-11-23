// BookPage on Linux (and Windows, once): a GtkPicture showing the engine's
// EQP1 raw bitmap, under a second picture that draws the highlights. The
// files are read here, not through Asuka: the pixels never touch the Asuka
// runtime.
//
// GTK's functions are asuka_linux.def's, called with G(name), as the
// host's own are (examples/media/segmented/desktop/segmented_linux.c).
#include "asuka_linux.h"
#include "bookpage.asuka.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	bookpage_BookPage base;
	bookpage_BookPage_events events;
	/// The bitmap's view, and the highlights' over it.
	GtkWidget *picture;
	GtkWidget *marks_picture;
	/// The page bitmap's size in device pixels, and pixels to a point.
	uint32_t width, height;
	double scale;
	/// The app's highlights, in the order it gave them.
	bookpage_list_Highlight marks;
	/// The app says it has a selection up: a drag adjusts it.
	gboolean has_selection;
	/// Where the pointer went down, and where it is now.
	double press_x, press_y;
	double last_x, last_y;
	/// Whether a button (or a finger) is down.
	gboolean down;
	/// Between starting a selection and lifting: motion extends it.
	gboolean dragging;
	/// A click just after a selection's lift is that selection's own, not a
	/// page turn.
	gint64 selection_ended;
} view;

static uint32_t le32(const unsigned char *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/// The highlights as one RGBA texture over the page: transparent but for
/// the rectangles, each in its own color. At the bitmap's own size, so the
/// two pictures line up whatever the window does.
static void paint_marks(view *v) {
	if (!v->marks_picture) return;
	if (v->width == 0 || v->height == 0 || v->marks.len == 0) {
		G(gtk_picture_set_paintable)((GtkPicture *)v->marks_picture, NULL);
		return;
	}
	gsize size = (gsize)v->width * v->height * 4;
	unsigned char *pixels = calloc(1, size);
	if (!pixels) return;
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
		uint32_t c = (uint32_t)h->color;
		unsigned char r = (c >> 24) & 0xff, g = (c >> 16) & 0xff, b = (c >> 8) & 0xff, a = c & 0xff;
		if (a == 0) continue;
		// A GDK texture's alpha is premultiplied; the highlight's is not.
		r = (unsigned char)((r * a + 127) / 255);
		g = (unsigned char)((g * a + 127) / 255);
		b = (unsigned char)((b * a + 127) / 255);
		for (long y = y0; y < y1; y++) {
			unsigned char *row = pixels + ((gsize)y * v->width + x0) * 4;
			for (long x = x0; x < x1; x++) {
				row[0] = r;
				row[1] = g;
				row[2] = b;
				row[3] = a;
				row += 4;
			}
		}
	}
	GBytes *bytes = G(g_bytes_new_take)(pixels, size);
	GdkTexture *texture =
		G(gdk_memory_texture_new)((int)v->width, (int)v->height, GDK_MEMORY_R8G8B8A8, bytes, (gsize)v->width * 4);
	G(g_bytes_unref)(bytes);
	if (!texture) return;
	G(gtk_picture_set_paintable)((GtkPicture *)v->marks_picture, (GdkPaintable *)texture);
	G(g_object_unref)(texture);
}

// The page's pixels, as a texture; the file's header says how big it is.
static void set_path(bookpage_BookPage *self, const char *path) {
	view *v = (view *)self;
	GtkWidget *picture = v->picture;
	if (!path || !*path) return;
	FILE *file = fopen(path, "rb");
	if (!file) return;
	unsigned char header[12];
	if (fread(header, 1, 12, file) != 12 || memcmp(header, "EQP1", 4) != 0) {
		fclose(file);
		return;
	}
	uint32_t width = le32(header + 4);
	uint32_t height = le32(header + 8);
	gsize size = (gsize)width * height * 4;
	unsigned char *pixels = size ? malloc(size) : NULL;
	if (!pixels || fread(pixels, 1, size, file) != size) {
		free(pixels);
		fclose(file);
		return;
	}
	fclose(file);
	GBytes *bytes = G(g_bytes_new_take)(pixels, size);
	GdkTexture *texture =
		G(gdk_memory_texture_new)((int)width, (int)height, GDK_MEMORY_R8G8B8A8, bytes, (gsize)width * 4);
	G(g_bytes_unref)(bytes);
	if (!texture) return;
	G(gtk_picture_set_paintable)((GtkPicture *)picture, (GdkPaintable *)texture);
	G(g_object_unref)(texture);
	v->width = width;
	v->height = height;
	paint_marks(v);
}

static void set_highlights(bookpage_BookPage *self, bookpage_list_Highlight value) {
	view *v = (view *)self;
	bookpage_list_Highlight_free(&v->marks);
	v->marks.len = value.len;
	v->marks.data = value.len ? calloc(value.len, sizeof *value.data) : NULL;
	if (value.len && !v->marks.data) v->marks.len = 0;
	for (size_t i = 0; i < v->marks.len; i++) v->marks.data[i] = value.data[i];
	paint_marks(v);
}

static void set_selecting(bookpage_BookPage *self, bool value) {
	view *v = (view *)self;
	v->has_selection = value;
}

static void set_scale(bookpage_BookPage *self, double value) {
	view *v = (view *)self;
	v->scale = value;
	paint_marks(v);
}

// A press only remembers where it was: a click is still a click, and a drag
// is what makes a selection.
static void pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
	(void)gesture;
	(void)n_press;
	view *v = data;
	if (!v->base.widget) return;
	v->press_x = v->last_x = x;
	v->press_y = v->last_y = y;
	v->down = 1;
}

// Motion while a button is down is a drag, and a drag makes a selection. This
// is its own event controller, not a gesture: a gesture could be denied by
// the click's claim on the same sequence, and then there would be no drag at
// all. A few points of jitter are still a click, unless a selection is up:
// then the drag adjusts it.
static void moved(GtkEventController *controller, double x, double y, gpointer data) {
	(void)controller;
	view *v = data;
	if (!v->base.widget) return;
	v->last_x = x;
	v->last_y = y;
	if (v->dragging) {
		bookpage_BookPage_drag(v->events, x, y);
		return;
	}
	if (!v->down) return;
	double dx = x - v->press_x, dy = y - v->press_y;
	if (dx * dx + dy * dy < 25 && !v->has_selection) return;
	v->dragging = 1;
	bookpage_BookPage_hold(v->events, v->press_x, v->press_y);
	bookpage_BookPage_drag(v->events, x, y);
}

// A touch screen holds a finger still before it drags: that starts a selection
// where the finger is. A mouse never waits; it drags.
static void held(GtkGestureLongPress *gesture, double x, double y, gpointer data) {
	(void)gesture;
	view *v = data;
	if (!v->base.widget || v->dragging) return;
	v->press_x = x;
	v->press_y = y;
	v->dragging = 1;
	bookpage_BookPage_hold(v->events, x, y);
}

// A lift: the end of a selection, or a tap that turns the page.
static void released(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
	(void)gesture;
	(void)n_press;
	view *v = data;
	if (!v->base.widget) return;
	v->down = 0;
	if (v->dragging) {
		v->dragging = 0;
		v->selection_ended = G(g_get_monotonic_time)();
		bookpage_BookPage_release(v->events);
		return;
	}
	// The click that comes with a selection's lift is not a page turn.
	if (v->selection_ended && G(g_get_monotonic_time)() - v->selection_ended < 800000) return;
	bookpage_BookPage_press(v->events, x, y);
}

static void destroy(bookpage_BookPage *self) {
	view *v = (view *)self;
	bookpage_list_Highlight_free(&v->marks);
	free(v);
}

static const bookpage_BookPage_vtable vtable = {
	.destroy = destroy,
	.set_path = set_path,
	.set_highlights = set_highlights,
	.set_selecting = set_selecting,
	.set_scale = set_scale,
};

bookpage_BookPage *bookpage_make_BookPage(bookpage_BookPage_events events) {
	(void)events;
	if (!asuka_linux()) return NULL;
	view *v = calloc(1, sizeof *v);
	if (!v) return NULL;
	v->base.vt = &vtable;
	v->events = events;
	v->scale = 1;

	GtkWidget *overlay = G(gtk_overlay_new)();
	GtkWidget *picture = G(gtk_picture_new)();
	G(gtk_picture_set_can_shrink)((GtkPicture *)picture, true);
	G(gtk_picture_set_content_fit)((GtkPicture *)picture, GTK_CONTENT_FIT_FILL);
	G(gtk_overlay_set_child)((GtkOverlay *)overlay, picture);

	// The highlights: another picture over the page's, never a target.
	GtkWidget *marks = G(gtk_picture_new)();
	G(gtk_picture_set_can_shrink)((GtkPicture *)marks, true);
	G(gtk_picture_set_content_fit)((GtkPicture *)marks, GTK_CONTENT_FIT_FILL);
	G(gtk_widget_set_can_target)(marks, false);
	G(gtk_widget_set_hexpand)(marks, true);
	G(gtk_widget_set_vexpand)(marks, true);
	G(gtk_overlay_add_overlay)((GtkOverlay *)overlay, marks);

	v->picture = picture;
	v->marks_picture = marks;

	// The page is the reader's surface: a click turns it, a drag selects text.
	GtkGesture *click = G(gtk_gesture_click_new)();
	G(gtk_gesture_single_set_button)((GtkGestureSingle *)click, 1);
	G(g_signal_connect_data)(click, "pressed", (GCallback)pressed, v, NULL, 0);
	G(g_signal_connect_data)(click, "released", (GCallback)released, v, NULL, 0);
	G(gtk_widget_add_controller)(overlay, (GtkEventController *)click);

	GtkEventController *motion = G(gtk_event_controller_motion_new)();
	G(g_signal_connect_data)(motion, "motion", (GCallback)moved, v, NULL, 0);
	G(gtk_widget_add_controller)(overlay, motion);

	// A mouse doesn't long-press; a finger does.
	GtkGesture *hold = G(gtk_gesture_long_press_new)();
	G(gtk_gesture_single_set_touch_only)((GtkGestureSingle *)hold, 1);
	G(g_signal_connect_data)(hold, "pressed", (GCallback)held, v, NULL, 0);
	G(gtk_widget_add_controller)(overlay, (GtkEventController *)hold);

	v->base.widget = overlay;
	return &v->base;
}
