// Where a font's file is, by family name, on Linux (and Windows, once): the
// distribution's own directories, probed by name. A reader that wants a
// table of families asks fontconfig; here a few paths cover the usual ones.
#include "asuka_linux.h"
#include "bookpage.asuka.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	bookpage_FontFiles base;
} fonts;

static struct {
	const char *family;
	const char *paths[3];
} known[] = {
	{"Songti SC", {"/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc", "/usr/share/fonts/truetype/arphic/uming.ttc", NULL}},
	{"PingFang SC", {"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", NULL}},
	{"WenQuanYi Zen Hei", {"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", NULL, NULL}},
	{"DejaVu Serif", {"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", NULL, NULL}},
};

static bool readable(const char *path) {
	if (!path) return false;
	FILE *file = fopen(path, "rb");
	if (!file) return false;
	fclose(file);
	return true;
}

static char *path(bookpage_FontFiles *self, const char *family, asuka_error *err) {
	(void)self;
	(void)err;
	if (family) {
		for (size_t i = 0; i < sizeof known / sizeof known[0]; i++) {
			if (strcmp(known[i].family, family) != 0) continue;
			for (size_t p = 0; p < 3 && known[i].paths[p]; p++) {
				if (readable(known[i].paths[p])) return asuka_sprintf("%s", known[i].paths[p]);
			}
		}
	}
	return asuka_sprintf("%s", "");
}

static const bookpage_FontFiles_vtable vtable = {.path = path};

// No release: the object is freed as the app lets it go (a NULL release).
bookpage_FontFiles *bookpage_make_FontFiles(asuka_error *err) {
	(void)err;
	fonts *it = calloc(1, sizeof *it);
	if (!it) return NULL;
	it->base.vt = &vtable;
	return &it->base;
}
