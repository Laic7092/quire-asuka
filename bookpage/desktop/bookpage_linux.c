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

// A tap, as the page's own view sees it: points from its top left.
static void pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
	(void)n_press;
	view *v = data;
	if (!v->base.widget) return;
	bookpage_BookPage_press(v->events, x, y);
	(void)gesture;
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
	// The page is the reader's touch surface: a tap turns it.
	GtkGesture *click = G(gtk_gesture_click_new)();
	G(gtk_gesture_single_set_button)((GtkGestureSingle *)click, 1);
	G(g_signal_connect_data)(click, "released", (GCallback)pressed, v, NULL, 0);
	G(gtk_widget_add_controller)(picture, (GtkEventController *)click);
	v->base.widget = picture;
	return &v->base;
}
