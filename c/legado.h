/*
 * legado-engine: the Legado (阅读3.0) book-source engine behind a C ABI.
 *
 * A handle owns the source HTTP clients (cookies, proxy), the rule engine,
 * the JavaScript runtime and the variable store. It can be used from several
 * threads at once: the JavaScript context, active source and request context
 * are thread-local, and cookies, HTTP clients and JS storage are locked.
 * Every const char* returned by a legado_* call is UTF-8 and is owned by the
 * engine: it stays valid until the next call on that thread (or, for
 * legado_sources, until the next legado_sources call). Copy it if you keep it.
 * A NULL answer means the call failed; read legado_last_error.
 *
 * Build: cargo build --release --lib -> target/release/liblegado_engine.a
 */
#ifndef LEGADO_ENGINE_H
#define LEGADO_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LegadoEngine LegadoEngine;

const char *legado_version(void);

/*
 * Open the engine. config_json may be NULL, or:
 *   {"storageDir": "/path", "proxy": null, "timeoutSecs": 30,
 *    "userNs": "app", "direct": false}
 *
 * storageDir holds the per-source cookie jars and the variables that
 * source.getVariable()/putVariable() write. direct ignores the system proxy.
 * Returns NULL on failure; legado_last_error(NULL) then says why.
 */
LegadoEngine *legado_open(const char *config_json);
void legado_close(LegadoEngine *engine);

/* The last error of this handle (empty when none), valid until the next call. */
const char *legado_last_error(const LegadoEngine *engine);

/*
 * Parse a source list (an array, or one object) into a JSON array of
 * {name, url, group, kind, enabled, comment, searchUrl, exploreUrl, hasJs}.
 * Entries without a bookSourceUrl are skipped. NULL on failure.
 */
const char *legado_sources(const char *json);

/* One page of search results: a JSON array of Legado SearchBook fields. */
const char *legado_search(LegadoEngine *engine, const char *source_json,
                    const char *key, int32_t page);

/* One page of an explore category: a JSON array of SearchBook fields. */
const char *legado_explore(LegadoEngine *engine, const char *source_json,
                     const char *url);

/* A book's detail page: a JSON Book (it carries tocUrl). */
const char *legado_info(LegadoEngine *engine, const char *source_json,
                  const char *book_url);

/* All chapters: a JSON array of BookChapter fields. */
const char *legado_toc(LegadoEngine *engine, const char *source_json,
                 const char *book_json);

/* One chapter's text, with nextContentUrl pages joined by a newline. */
const char *legado_content(LegadoEngine *engine, const char *source_json,
                     const char *book_json, const char *chapter_json);

/* The source's Cookie header for a URL. */
const char *legado_cookie_header(LegadoEngine *engine, const char *source_json,
                           const char *url);

/* Add a Cookie header to the source's jar. */
const char *legado_set_cookie(LegadoEngine *engine, const char *source_json,
                        const char *url, const char *cookie);

/* Debug: run a JavaScript rule with the source bindings. */
const char *legado_eval(LegadoEngine *engine, const char *source_json,
                  const char *script, const char *input,
                  const char *base_url);

/* Debug: run a full rule (CSS / XPath / JSONPath / JS) against text. */
const char *legado_eval_rule(LegadoEngine *engine, const char *source_json,
                       const char *rule, const char *body,
                       const char *base_url);


#ifdef __cplusplus
}
#endif

#endif
