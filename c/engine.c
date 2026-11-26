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

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ebook_abi.h"

// The engine handle is a `Mutex<Engine>` inside: any thread may call it, and
// calls queue there rather than race. Asuka's `spawn` still won't take an
// extern type across threads, so a band is drawn through a lease: a pointer
// an Asuka int can carry, with one more reference behind it. The lease lives
// in this table until the C call that draws the band gives it back.
//
// An entry starts at two references: the caller's handle and the lease. The
// handle's own release is the same `quire_engine_free`; whichever side lets
// go last closes the book.
typedef struct {
    EbEngine *engine;
    unsigned refs;
} EngineLease;

static EngineLease *engine_leases;
static size_t engine_lease_count;
static size_t engine_lease_cap;
static atomic_flag engine_lease_lock = ATOMIC_FLAG_INIT;

static void lease_lock(void) {
    while (atomic_flag_test_and_set_explicit(&engine_lease_lock, memory_order_acquire)) {
    }
}

static void lease_unlock(void) {
    atomic_flag_clear_explicit(&engine_lease_lock, memory_order_release);
}

// Adds a lease on engine. False when the table couldn't grow.
static bool lease_hold(EbEngine *engine) {
    bool ok = true;
    lease_lock();
    for (size_t i = 0; i < engine_lease_count; i++) {
        if (engine_leases[i].engine == engine) {
            engine_leases[i].refs++;
            goto out;
        }
    }
    if (engine_lease_count == engine_lease_cap) {
        size_t cap = engine_lease_cap == 0 ? 8 : engine_lease_cap * 2;
        EngineLease *grown = realloc(engine_leases, cap * sizeof *grown);
        if (grown == NULL) {
            ok = false;
            goto out;
        }
        engine_leases = grown;
        engine_lease_cap = cap;
    }
    engine_leases[engine_lease_count].engine = engine;
    engine_leases[engine_lease_count].refs = 2;  // the handle, and this lease
    engine_lease_count++;
out:
    lease_unlock();
    return ok;
}

// Drops one reference; true when there was no lease left and the book is the
// last one to close.
static bool lease_drop(EbEngine *engine) {
    bool last = true;
    lease_lock();
    for (size_t i = 0; i < engine_lease_count; i++) {
        if (engine_leases[i].engine != engine) {
            continue;
        }
        if (engine_leases[i].refs > 1) {
            engine_leases[i].refs--;
            last = false;
        } else {
            engine_leases[i] = engine_leases[--engine_lease_count];
        }
        break;
    }
    lease_unlock();
    return last;
}

void quire_engine_free(EbEngine *engine) {
    if (engine != NULL && lease_drop(engine)) {
        eb_plain_close(engine);
    }
}

// A lease on engine as the integer `spawn` can carry; 0 when none.
uint64_t quire_engine_lease(EbEngine *engine) {
    if (engine == NULL || !lease_hold(engine)) {
        return 0;
    }
    return (uint64_t)(uintptr_t)engine;
}

// Draws one band on the thread that took the lease, and gives the lease back
// even when the band fails. A cancelled `spawn` still runs to its end, so the
// lease can't outlive the work.
int32_t quire_flow_band_lease(uint64_t address, float y, float height,
                              float scale, uint32_t ink, uint32_t paper,
                              bool bold, const char *path) {
    EbEngine *engine = (EbEngine *)(uintptr_t)address;
    int32_t rc = -1;
    if (engine != NULL && height > 0) {
        rc = eb_plain_scroll_band_raw(engine, y, height, scale, ink, paper,
                                      bold, path);
    }
    quire_engine_free(engine);
    return rc;
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
