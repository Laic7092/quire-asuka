// The book engine's C ABI, shaped for Asuka.
//
// ebook-engine 0.5.1's scalar surface takes an opaque `EbEngine *` and, for
// layout, a `const EbLayoutOptions *`; a selection is an `EbSelection *` whose
// rectangles and text only the library can read. Asuka's `extern` passes
// structs by value and can't read memory behind a `Pointer`, so this file is
// the thin adapter:
//
//   * the layout options (size/version + viewport + style) are built here from
//     scalars, so engine.asuka keeps its flat `layout(...)` call;
//   * a selection's parts come back one at a time from its snapshot pointer;
//   * full-text search (`Request::Search`) is on the postcard face only, so
//     the request's few bytes are written here and the answer is kept as a
//     handle whose hits come back one field at a time (see `quire_search`);
//   * `quire_cstr` turns a `const char *` field of a by-value struct back into
//     a string (the pointer stays the library's, valid until the book closes);
//   * both release functions have the signature Asuka's `extern type` wants
//     (one pointer, no result).
//
// `ebook_abi.h` beside this file is the 0.5.1 header: the library for the host
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

// ---- Full-text search ----
//
// The scalar surface has no search call: the engine added it to the postcard
// enum (`Request::Search { query, limit }` → `Response::Search(SearchResults)`,
// both appended at the end so the old tags don't move). Asuka can't read a
// buffer behind a `Pointer`, so the request goes out from here and the answer
// is decoded into a handle whose fields come back one at a time — the same
// shape a selection's snapshot has.
//
// postcard is field order, varint lengths and varint unsigned integers; the
// only bytes written here are the variant tag, the query and the limit. The
// response is checked at every step: a short or malformed buffer is a failed
// search, never a read past the end.

// The two enum tags, counted from `crates/ebook-abi/src/protocol.rs` (search
// was appended to both enums, so these stay put as variants are added).
#define REQUEST_SEARCH 41u
#define RESPONSE_SEARCH 30u

// One hit as the engine sent it. Strings are copies: the response buffer is
// freed as soon as it has been read.
typedef struct {
    uint32_t spine;
    uint32_t node;
    uint32_t offset;
    uint32_t end;
    bool ellipsis_before;
    bool ellipsis_after;
    // "" when the chapter has no table-of-contents entry.
    char *chapter;
    char *before;
    char *text;
    char *after;
} QuireFindHit;

// A whole search: the hits, and what the engine did while finding them.
typedef struct {
    uint32_t count;
    QuireFindHit *hits;
    uint32_t truncated;
    uint32_t scanned;
    uint32_t skipped;
} QuireFind;

void quire_find_free(QuireFind *find) {
    if (find == NULL) {
        return;
    }
    for (uint32_t i = 0; i < find->count; i++) {
        free(find->hits[i].chapter);
        free(find->hits[i].before);
        free(find->hits[i].text);
        free(find->hits[i].after);
    }
    free(find->hits);
    free(find);
}

// A cursor over the response, bounded by its length.
typedef struct {
    const uint8_t *data;
    size_t len;
    size_t at;
    bool bad;
} PostcardReader;

// postcard's varint: little-endian base 128, high bit says "more".
static uint64_t read_varint(PostcardReader *reader) {
    uint64_t value = 0;
    unsigned shift = 0;
    while (reader->at < reader->len) {
        uint8_t byte = reader->data[reader->at++];
        value |= (uint64_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            return value;
        }
        shift += 7;
        if (shift > 63) {
            break;
        }
    }
    reader->bad = true;
    return 0;
}

static char *read_string(PostcardReader *reader) {
    uint64_t len = read_varint(reader);
    if (reader->bad || len > (uint64_t)(reader->len - reader->at)) {
        reader->bad = true;
        return NULL;
    }
    char *out = malloc((size_t)len + 1);
    if (out == NULL) {
        reader->bad = true;
        return NULL;
    }
    memcpy(out, reader->data + reader->at, (size_t)len);
    out[len] = '\0';
    reader->at += (size_t)len;
    return out;
}

static bool read_bool(PostcardReader *reader) {
    return read_varint(reader) != 0;
}

static char *empty_string(void) {
    char *out = malloc(1);
    if (out != NULL) {
        out[0] = '\0';
    }
    return out;
}

// One Response::Search, decoded. NULL on anything that isn't one.
static QuireFind *decode_find(const uint8_t *data, size_t len) {
    PostcardReader reader = {data, len, 0, false};
    if (read_varint(&reader) != RESPONSE_SEARCH || reader.bad) {
        return NULL;
    }
    QuireFind *find = calloc(1, sizeof(*find));
    if (find == NULL) {
        return NULL;
    }
    uint32_t count = (uint32_t)read_varint(&reader);
    // Every hit is more than one byte long: a count larger than the buffer is
    // not an answer this library sent.
    if (reader.bad || count > len) {
        free(find);
        return NULL;
    }
    if (count > 0) {
        find->hits = calloc(count, sizeof(*find->hits));
        if (find->hits == NULL) {
            free(find);
            return NULL;
        }
    }
    for (uint32_t i = 0; i < count && !reader.bad; i++) {
        QuireFindHit *hit = &find->hits[i];
        // Count the hit before filling it: a string that failed to read is
        // NULL, and freeing the ones already there is what free(NULL) does.
        find->count = i + 1;
        hit->spine = (uint32_t)read_varint(&reader);
        hit->node = (uint32_t)read_varint(&reader);
        hit->offset = (uint32_t)read_varint(&reader);
        hit->end = (uint32_t)read_varint(&reader);
        bool has_chapter = read_bool(&reader);
        hit->chapter = has_chapter ? read_string(&reader) : empty_string();
        hit->before = read_string(&reader);
        hit->text = read_string(&reader);
        hit->after = read_string(&reader);
        hit->ellipsis_before = read_bool(&reader);
        hit->ellipsis_after = read_bool(&reader);
        if (hit->chapter == NULL || hit->before == NULL || hit->text == NULL ||
            hit->after == NULL) {
            reader.bad = true;
        }
    }
    if (!reader.bad) {
        find->truncated = read_bool(&reader) ? 1 : 0;
        find->scanned = (uint32_t)read_varint(&reader);
        find->skipped = (uint32_t)read_varint(&reader);
    }
    if (reader.bad) {
        quire_find_free(find);
        return NULL;
    }
    return find;
}

static void put_varint(uint8_t *out, size_t *at, uint64_t value) {
    while (value >= 0x80) {
        out[(*at)++] = (uint8_t)((value & 0x7f) | 0x80);
        value >>= 7;
    }
    out[(*at)++] = (uint8_t)value;
}

// Raises `Request::Search` and keeps the answer. NULL when the engine refused
// it (or the answer didn't parse); an empty query is the engine's own answer:
// no hits, nothing scanned.
QuireFind *quire_search(EbEngine *engine, const char *query, uint32_t limit) {
    if (engine == NULL || query == NULL) {
        return NULL;
    }
    size_t query_len = strlen(query);
    // postcard writes the length as a varint; five bytes holds a u32, and a
    // query that doesn't fit one isn't a query this app can pass anyway.
    if (query_len > UINT32_MAX) {
        return NULL;
    }
    uint8_t *request = malloc(1 + 5 + query_len + 5);
    if (request == NULL) {
        return NULL;
    }
    size_t at = 0;
    put_varint(request, &at, REQUEST_SEARCH);
    put_varint(request, &at, query_len);
    memcpy(request + at, query, query_len);
    at += query_len;
    put_varint(request, &at, limit);

    size_t response_len = 0;
    uint8_t *response = eb_request(engine, request, at, &response_len);
    free(request);
    if (response == NULL || response_len == 0) {
        eb_free(response, response_len);
        return NULL;
    }
    QuireFind *find = decode_find(response, response_len);
    eb_free(response, response_len);
    return find;
}

static const QuireFindHit *find_hit(const QuireFind *find, uint32_t index) {
    if (find == NULL || index >= find->count) {
        return NULL;
    }
    return &find->hits[index];
}

uint32_t quire_find_count(const QuireFind *find) {
    return find != NULL ? find->count : 0;
}

uint32_t quire_find_spine(const QuireFind *find, uint32_t index) {
    const QuireFindHit *hit = find_hit(find, index);
    return hit != NULL ? hit->spine : 0;
}

uint32_t quire_find_node(const QuireFind *find, uint32_t index) {
    const QuireFindHit *hit = find_hit(find, index);
    return hit != NULL ? hit->node : 0;
}

uint32_t quire_find_offset(const QuireFind *find, uint32_t index) {
    const QuireFindHit *hit = find_hit(find, index);
    return hit != NULL ? hit->offset : 0;
}

uint32_t quire_find_end(const QuireFind *find, uint32_t index) {
    const QuireFindHit *hit = find_hit(find, index);
    return hit != NULL ? hit->end : 0;
}

const char *quire_find_chapter(const QuireFind *find, uint32_t index) {
    const QuireFindHit *hit = find_hit(find, index);
    return hit != NULL ? hit->chapter : "";
}

const char *quire_find_before(const QuireFind *find, uint32_t index) {
    const QuireFindHit *hit = find_hit(find, index);
    return hit != NULL ? hit->before : "";
}

const char *quire_find_text(const QuireFind *find, uint32_t index) {
    const QuireFindHit *hit = find_hit(find, index);
    return hit != NULL ? hit->text : "";
}

const char *quire_find_after(const QuireFind *find, uint32_t index) {
    const QuireFindHit *hit = find_hit(find, index);
    return hit != NULL ? hit->after : "";
}

// Bit 0: the context before was cut; bit 1: the one after was.
uint32_t quire_find_flags(const QuireFind *find, uint32_t index) {
    const QuireFindHit *hit = find_hit(find, index);
    if (hit == NULL) {
        return 0;
    }
    return (hit->ellipsis_before ? 1u : 0u) | (hit->ellipsis_after ? 2u : 0u);
}

uint32_t quire_find_truncated(const QuireFind *find) {
    return find != NULL ? find->truncated : 0;
}

uint32_t quire_find_scanned(const QuireFind *find) {
    return find != NULL ? find->scanned : 0;
}

uint32_t quire_find_skipped(const QuireFind *find) {
    return find != NULL ? find->skipped : 0;
}

// A `const char *` a by-value struct carried, as the string it is. The bytes
// are still the library's (a handle's pool, or the empty string); Asuka copies
// them into a string at the call.
const char *quire_cstr(const void *pointer) {
    return pointer != NULL ? (const char *)pointer : "";
}

