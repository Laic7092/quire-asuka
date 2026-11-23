// BookPage on Linux (and Windows, once): a GtkPicture showing the engine's
// EQP1 raw bitmap. The file is read here, not through Asuka: the pixels
// never touch the Asuka runtime.
//
// GTK's functions are asuka_linux.def's, called with G(name), as the
// host's own are (examples/media/segmented/desktop/segmented_linux.c).
#include "asuka_linux.h"
#include "bookpage.asuka.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	bookpage_BookPage base;
	bookpage_BookPage_events events;
	/// Where the pointer went down, and where it is now.
	double press_x, press_y;
	double last_x, last_y;
	/// Whether a button (or a finger) is down.
	gboolean down;
	/// Between starting a selection and lifting: motion extends it.
	gboolean selecting;
	/// A click just after a selection's lift is that selection's own, not a
	/// page turn.
	gint64 selection_ended;
} view;

static uint32_t le32(const unsigned char *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// The page's pixels, as a texture; the file's header says how big it is.
static void set_path(bookpage_BookPage *self, const char *path) {
	GtkWidget *picture = self->widget;
	if (!path || !*path) return;
	FILE *file = fopen(path, "rb");
	if (!file) return;
	unsigned char header[12];
	uint32_t width = 0, height = 0;
	if (fread(header, 1, 12, file) != 12 || memcmp(header, "EQP1", 4) != 0) {
		fclose(file);
		return;
	}
	width = le32(header + 4);
	height = le32(header + 8);
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
// all. A few points of jitter are still a click.
static void moved(GtkEventController *controller, double x, double y, gpointer data) {
	(void)controller;
	view *v = data;
	if (!v->base.widget) return;
	v->last_x = x;
	v->last_y = y;
	if (v->selecting) {
		bookpage_BookPage_drag(v->events, x, y);
		return;
	}
	if (!v->down) return;
	double dx = x - v->press_x, dy = y - v->press_y;
	if (dx * dx + dy * dy < 25) return;
	v->selecting = 1;
	bookpage_BookPage_hold(v->events, v->press_x, v->press_y);
	bookpage_BookPage_drag(v->events, x, y);
}

// A touch screen holds a finger still before it drags: that starts a selection
// where the finger is. A mouse never waits; it drags.
static void held(GtkGestureLongPress *gesture, double x, double y, gpointer data) {
	(void)gesture;
	view *v = data;
	if (!v->base.widget || v->selecting) return;
	v->press_x = x;
	v->press_y = y;
	v->selecting = 1;
	bookpage_BookPage_hold(v->events, x, y);
}

// A lift: the end of a selection, or a tap that turns the page.
static void released(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
	(void)gesture;
	(void)n_press;
	view *v = data;
	if (!v->base.widget) return;
	v->down = 0;
	if (v->selecting) {
		v->selecting = 0;
		v->selection_ended = G(g_get_monotonic_time)();
		bookpage_BookPage_release(v->events);
		return;
	}
	// The click that comes with a selection's lift is not a page turn.
	if (v->selection_ended && G(g_get_monotonic_time)() - v->selection_ended < 800000) return;
	bookpage_BookPage_press(v->events, x, y);
}

static void destroy(bookpage_BookPage *self) { free(self); }

static const bookpage_BookPage_vtable vtable = {
	.destroy = destroy,
	.set_path = set_path,
};

bookpage_BookPage *bookpage_make_BookPage(bookpage_BookPage_events events) {
	(void)events;
	if (!asuka_linux()) return NULL;
	view *v = calloc(1, sizeof *v);
	if (!v) return NULL;
	v->base.vt = &vtable;
	v->events = events;
	GtkWidget *picture = G(gtk_picture_new)();
	G(gtk_picture_set_can_shrink)((GtkPicture *)picture, true);
	G(gtk_picture_set_content_fit)((GtkPicture *)picture, GTK_CONTENT_FIT_FILL);

	// The page is the reader's surface: a click turns it, a drag selects text.
	GtkGesture *click = G(gtk_gesture_click_new)();
	G(gtk_gesture_single_set_button)((GtkGestureSingle *)click, 1);
	G(g_signal_connect_data)(click, "pressed", (GCallback)pressed, v, NULL, 0);
	G(g_signal_connect_data)(click, "released", (GCallback)released, v, NULL, 0);
	G(gtk_widget_add_controller)(picture, (GtkEventController *)click);

	GtkEventController *motion = G(gtk_event_controller_motion_new)();
	G(g_signal_connect_data)(motion, "motion", (GCallback)moved, v, NULL, 0);
	G(gtk_widget_add_controller)(picture, motion);

	// A mouse doesn't long-press; a finger does.
	GtkGesture *hold = G(gtk_gesture_long_press_new)();
	G(gtk_gesture_single_set_touch_only)((GtkGestureSingle *)hold, 1);
	G(g_signal_connect_data)(hold, "pressed", (GCallback)held, v, NULL, 0);
	G(gtk_widget_add_controller)(picture, (GtkEventController *)hold);

	v->base.widget = picture;
	return &v->base;
}
