/*
 * legado-engine: the Legado (阅读 3.0) book-source engine behind a C ABI.
 *
 * A handle owns the HTTP clients (cookies, proxy), the rule engine, the
 * JavaScript runtime and the variable store. It may be used from several
 * threads at once: JS context, active source and request context are
 * thread-local; cookies, clients and JS storage are locked.
 *
 * Every returned const char* is UTF-8, owned by the engine and valid until the
 * next call on the same thread (legado_sources: until the next
 * legado_sources call). Copy it to keep it. NULL means the call failed; read
 * legado_last_error.
 *
 * Cancellation: legado_cancel() stops what the handle is doing, from any
 * thread. Cancelled calls return NULL with legado_last_error() exactly
 * LEGADO_CANCELLED; calls started afterwards run normally.
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

/* legado_last_error() of a cancelled call; compare with strcmp. */
#define LEGADO_CANCELLED "cancelled"

const char *legado_version(void);

/*
 * Open the engine. config_json may be NULL, or:
 *   {"storageDir": "/path", "proxy": null, "timeoutSecs": 15,
 *    "userNs": "app", "direct": false}
 *
 * storageDir holds per-source cookie jars, source variables and java.cacheFile
 * data; without it they live in memory. userNs scopes those files. direct
 * ignores the system proxy.
 *
 * timeoutSecs is the total budget of one request (default 15); connecting is
 * capped at 10s and a stalled read at 60s. legado_download ignores it: 30
 * minutes total, giving up only when the connection stalls for 60s.
 *
 * Returns NULL on failure; legado_last_error(NULL) says why.
 */
LegadoEngine *legado_open(const char *config_json);
void legado_close(LegadoEngine *engine);

/*
 * Cancel every call this handle is running and return immediately. Safe from
 * any thread, including while another thread is inside a call, and safe before
 * any call was made. Not sticky: later calls run normally.
 */
void legado_cancel(LegadoEngine *engine);

/* The last error of this handle (empty when none), valid until the next call. */
const char *legado_last_error(const LegadoEngine *engine);

/*
 * Parse a source list (an array or a single object) into a JSON array of
 * {name, url, group, kind, enabled, comment, searchUrl, exploreUrl, hasJs}.
 * Entries without a bookSourceUrl are skipped. NULL on failure.
 */
const char *legado_sources(const char *json);

/* One page of search results: a JSON array of SearchBook fields. */
const char *legado_search(LegadoEngine *engine, const char *source_json,
                    const char *key, int32_t page);

/* First page of an explore category: a JSON array of SearchBook fields. */
const char *legado_explore(LegadoEngine *engine, const char *source_json,
                     const char *url);

/*
 * One page of an explore category, filling {{key}} and {{page}} in the
 * category URL (key may be NULL). legado_explore is this call with key="" and
 * page=1, so a host that never calls this one cannot page 发现.
 */
const char *legado_explore_page(LegadoEngine *engine, const char *source_json,
                          const char *url, const char *key, int32_t page);

/* A book's detail page: a JSON Book (carries tocUrl). */
const char *legado_info(LegadoEngine *engine, const char *source_json,
                  const char *book_url);

/* All chapters: a JSON array of BookChapter fields. */
const char *legado_toc(LegadoEngine *engine, const char *source_json,
                 const char *book_json);

/* One chapter's text; nextContentUrl pages are joined with a newline. */
const char *legado_content(LegadoEngine *engine, const char *source_json,
                     const char *book_json, const char *chapter_json);

/*
 * Download a file with the source's headers, cookies and proxy to dest_path.
 * Answers {"path":…,"size":123,"contentType":null,"charset":"gb18030","url":…}.
 * Parent directories are created; the file appears atomically (written to
 * .part, then renamed). Bytes are verbatim: parse with the reported charset.
 *
 * This is how download sources (bookSourceType 3) deliver their book:
 * ruleBookInfo.downloadUrls holds one "name::url" per line (bare URL allowed).
 */
const char *legado_download(LegadoEngine *engine, const char *source_json,
                      const char *url, const char *dest_path);

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

/*
 * Import a `legado://import/…` share link, URL, base64, gzip payload or raw
 * JSON. engine may be NULL when the payload is inline.
 * Answers {"declaredKind":…, "url":…, "origin":"url|inline|base64",
 *          "bookSources":[…], "rssSources":[…], "replaceRules":[…],
 *          "unknown":0}
 *
 * Arrays hold the original JSON objects (unknown fields survive) classified by
 * shape, so a mixed collection lands in the right buckets; declaredKind is only
 * the link's intent. `unknown` counts objects matching nothing: warn about a
 * partial import instead of pretending it all came through.
 *
 * Deduplicate on bookSourceUrl / sourceUrl; the engine keeps no database.
 */
const char *legado_import(LegadoEngine *engine, const char *text);

/*
 * Replace rules (净化) over text. No network, no handle needed.
 *
 * rules_json    a ReplaceRule array
 * context_json  NULL or "", or {"bookName":…,"bookUrl":…,"sourceUrl":…}
 *               for scope / excludeScope matching
 * target        NULL / "content", or "title"
 *
 * Answers {"text":…,"applied":[…],"skipped":[…],"errors":[…]}.
 * Filtered rules ("disabled", "scope", "title", "content", "emptyPattern") and
 * patterns this engine cannot compile are reported, never silently ignored;
 * applied[].changed separates "rewrote the text" from "matched nothing".
 *
 * Feed it the raw text from legado_content: cleaning at read time keeps cached
 * chapters in sync with edits to the rule list.
 */
const char *legado_replace(const char *rules_json, const char *text,
                      const char *context_json, const char *target);

/*
 * RSS (订阅源).
 *
 * legado_rss_sources answers, for a list of sources,
 *   {name, url, group, enabled, comment, sortUrl, singleUrl, hasJs}
 * where sortUrl is the category JSON (parse it yourself, as with exploreUrl).
 *
 * legado_rss_articles answers one list page
 *   {"articles":[…], "nextUrl":"…"}
 * with sort_url NULL meaning the source's own sourceUrl.
 *
 * legado_rss_content answers one article's body, following ruleNextPage.
 */
const char *legado_rss_sources(const char *json);
const char *legado_rss_articles(LegadoEngine *engine, const char *source_json,
                          const char *sort_url);
const char *legado_rss_content(LegadoEngine *engine, const char *source_json,
                         const char *article_json);

#ifdef __cplusplus
}
#endif

#endif
