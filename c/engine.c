// The book engine's C ABI, shaped for Asuka.
//
// ebook-engine 0.4.0's scalar surface takes an opaque `EbEngine *` and, for
// layout, a `const EbLayoutOptions *`; a selection is an `EbSelection *` whose
// rectangles and text only the library can read. Asuka's `extern` passes
// structs by value and can't read memory behind a `Pointer`, so this file is
// the thin adapter:
//
//   * the layout options (size/version + viewport + style) are built here from
//     scalars, so engine.asuka keeps its flat `layout(...)` call;
//   * a selection's parts come back one at a time from its snapshot pointer;
//   * `quire_cstr` turns a `const char *` field of a by-value struct back into
//     a string (the pointer stays the library's, valid until the book closes);
//   * both release functions have the signature Asuka's `extern type` wants
//     (one pointer, no result).
//
// `ebook_abi.h` beside this file is the 0.4.0 header: the library for the host
// is in `lib/<target>/`, and both are the same release.

#include <string.h>

#include "ebook_abi.h"

void quire_engine_free(EbEngine *engine) {
    if (engine != NULL) {
        eb_plain_close(engine);
    }
}

void quire_selection_free(EbSelection *selection) {
    eb_plain_selection_free(selection);
}

static void layout_options(EbLayoutOptions *options, uint32_t spine, uint32_t scroll,
                           float width, float height, float top, float right,
                           float bottom, float left, float font_scale,
                           float line_scale, float letter_px, float word_px,
                           uint32_t align) {
    memset(options, 0, sizeof(*options));
    options->size = (uint32_t)sizeof(*options);
    options->version = EB_LAYOUT_OPTIONS_VERSION;
    options->spine = spine;
    options->scroll = scroll;
    options->width = width;
    options->height = height;
    options->top = top;
    options->right = right;
    options->bottom = bottom;
    options->left = left;
    options->font_scale = font_scale;
    options->line_scale = line_scale;
    options->letter_px = letter_px;
    options->word_px = word_px;
    options->align = align;
}

// Lays a chapter out; `scroll` picks the page flow or the chapter's column.
int32_t quire_layout(EbEngine *engine, uint32_t spine, uint32_t scroll,
                     float width, float height, float top, float right,
                     float bottom, float left, float font_scale,
                     float line_scale, float letter_px, float word_px,
                     uint32_t align) {
    EbLayoutOptions options;
    layout_options(&options, spine, scroll, width, height, top, right, bottom,
                   left, font_scale, line_scale, letter_px, word_px, align);
    return eb_plain_layout(engine, &options);
}

// Opens the whole-book scroll session at this viewport and style.
int32_t quire_scroll_begin(EbEngine *engine, float width, float height,
                           float top, float right, float bottom, float left,
                           float font_scale, float line_scale, float letter_px,
                           float word_px, uint32_t align) {
    EbLayoutOptions options;
    layout_options(&options, 0, 0, width, height, top, right, bottom, left,
                   font_scale, line_scale, letter_px, word_px, align);
    return eb_plain_scroll_begin(engine, &options);
}

// Lays a chapter out and takes one scroll window of it, in one call.
int32_t quire_scroll_slice(EbEngine *engine, uint32_t spine, float width,
                           float height, float top, float right, float bottom,
                           float left, float font_scale, float line_scale,
                           float letter_px, float word_px, uint32_t align,
                           float y, float slice_height, float scale, uint32_t ink,
                           uint32_t paper, bool bold, const char *path) {
    EbScrollSliceOptions options;
    memset(&options, 0, sizeof(options));
    options.size = (uint32_t)sizeof(options);
    options.version = EB_SCROLL_SLICE_OPTIONS_VERSION;
    layout_options(&options.layout, spine, 1, width, height, top, right, bottom,
                   left, font_scale, line_scale, letter_px, word_px, align);
    options.y = y;
    options.slice_height = slice_height;
    options.scale = scale;
    options.ink = ink;
    options.paper = paper;
    options.bold = bold;
    return eb_plain_scroll_slice_raw(engine, &options, path);
}

// The four parts of a snapshot. Each checks for the empty selection Asuka's
// optional handle gives for NULL.
const char *quire_selection_text(const EbSelection *selection) {
    return selection != NULL && selection->text != NULL ? selection->text : "";
}

uint32_t quire_selection_rect_count(const EbSelection *selection) {
    return selection != NULL ? selection->rect_count : 0;
}

EbSelectionRect quire_selection_rect(const EbSelection *selection, uint32_t index) {
    EbSelectionRect rect;
    memset(&rect, 0, sizeof(rect));
    if (selection == NULL || index >= selection->rect_count) {
        return rect;
    }
    return selection->rects[index];
}

EbLoc quire_selection_start(const EbSelection *selection) {
    EbLoc loc;
    memset(&loc, 0, sizeof(loc));
    if (selection != NULL) {
        loc = selection->start;
    } else {
        loc.page = -1;
        loc.progress = -1;
    }
    return loc;
}

EbLoc quire_selection_end(const EbSelection *selection) {
    EbLoc loc;
    memset(&loc, 0, sizeof(loc));
    if (selection != NULL) {
        loc = selection->end;
    } else {
        loc.page = -1;
        loc.progress = -1;
    }
    return loc;
}

// A `const char *` a by-value struct carried, as the string it is. The bytes
// are still the library's (a handle's pool, or the empty string); Asuka copies
// them into a string at the call.
const char *quire_cstr(const void *pointer) {
    return pointer != NULL ? (const char *)pointer : "";
}
