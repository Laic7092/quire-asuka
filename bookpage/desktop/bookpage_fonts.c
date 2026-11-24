// Where a font's file is, by family name, on Linux and Windows: the
// system's own directories, probed by name. A reader that wants a table of
// families asks the platform's font layer; here a few paths cover the usual
// ones.
#if defined(_WIN32)
#include "asuka_windows.h"
#else
#include "asuka_linux.h"
#endif
#include "bookpage.asuka.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	bookpage_FontFiles base;
} fonts;

static struct {
	const char *family;
	const char *files[3];
} known[] = {
#if defined(_WIN32)
	// Windows' Chinese faces: SimSun is the serif, YaHei the sans.
	{"Songti SC", {"simsun.ttc", NULL, NULL}},
	{"PingFang SC", {"msyh.ttc", "msyh.ttf", NULL}},
	{"Hiragino Sans GB", {"msyh.ttc", NULL, NULL}},
	{"Songti TC", {"simsun.ttc", NULL, NULL}},
	{"Noto Serif CJK SC", {"NotoSerifCJK-Regular.ttc", "simsun.ttc", NULL}},
	{"Microsoft YaHei", {"msyh.ttc", "msyh.ttf", NULL}},
	{"SimSun", {"simsun.ttc", NULL, NULL}},
#else
	{"Songti SC", {"/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc", "/usr/share/fonts/truetype/arphic/uming.ttc", NULL}},
	{"PingFang SC", {"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", NULL}},
	{"Noto Serif CJK SC", {"/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc", NULL, NULL}},
	{"WenQuanYi Zen Hei", {"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", NULL, NULL}},
	{"DejaVu Serif", {"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", NULL, NULL}},
#endif
};

/// The file a name stands for here: Windows' fonts live in one directory,
/// the distribution's are named in full.
static char *at(const char *name) {
#if defined(_WIN32)
	char dir[MAX_PATH];
	UINT n = GetWindowsDirectoryA(dir, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return NULL;
	return asuka_sprintf("%s/Fonts/%s", dir, name);
#else
	return name ? asuka_sprintf("%s", name) : NULL;
#endif
}

static bool readable(const char *path) {
	if (!path) return false;
#if defined(_WIN32)
	wchar_t *name = asuka_wide(path);
	FILE *file = name ? _wfopen(name, L"rb") : NULL;
	free(name);
#else
	FILE *file = fopen(path, "rb");
#endif
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
			for (size_t f = 0; f < 3 && known[i].files[f]; f++) {
				char *p = at(known[i].files[f]);
				if (readable(p)) return p;
				free(p);
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
