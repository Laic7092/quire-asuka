// legado-engine, behind one handle this app keeps: the book sources' HTTP
// clients, cookies, variables, rules and JavaScript. legado-engine 0.3.0 can
// use one handle from several threads at once (0.2.0 made the ABI's answers
// per calling thread), so the app's searches overlap; an older engine still
// gets one call at a time, behind the lock below.
//
// A platform with the engine's static library built (Linux, macOS, an iPhone)
// compiles the calls below; one without it (the iOS simulator, Windows for
// now, Android, the web) compiles the same functions as answers saying so, so
// the app still builds and says book sources aren't available there.
//
// The `const char *` answers are copies in a buffer of the calling thread:
// legado-engine's own pointer is only valid until its next call, and another
// thread's call may come at any time. The app copies the string at once.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if (defined(__linux__) && !defined(__ANDROID__)) || \
    (defined(__APPLE__) && !TARGET_OS_SIMULATOR)
#define QUIRE_HAS_LEGADO 1
#endif

#ifdef QUIRE_HAS_LEGADO

#include <pthread.h>

#include "legado.h"

static pthread_mutex_t legado_lock = PTHREAD_MUTEX_INITIALIZER;
static LegadoEngine *legado_handle = NULL;

/// The calling thread's copy of the last answer, and its last error. Kept
/// apart so two threads' calls don't overwrite each other's strings.
#if defined(_MSC_VER)
#define QUIRE_THREAD_LOCAL __declspec(thread)
#else
#define QUIRE_THREAD_LOCAL _Thread_local
#endif

static QUIRE_THREAD_LOCAL char *answer = NULL;
static QUIRE_THREAD_LOCAL size_t answer_cap = 0;
static QUIRE_THREAD_LOCAL char failure[1024] = "";

static const char *keep(const char *text) {
  if (text == NULL) return NULL;
  size_t size = strlen(text) + 1;
  if (size > answer_cap) {
    char *grown = (char *)realloc(answer, size);
    if (grown == NULL) return NULL;
    answer = grown;
    answer_cap = size;
  }
  memcpy(answer, text, size);
  return answer;
}

static void remember_failure(LegadoEngine *engine) {
  const char *reason = legado_last_error(engine);
  if (reason == NULL || reason[0] == '\0') {
    reason = "legado-engine failed";
  }
  snprintf(failure, sizeof failure, "%s", reason);
}

/// The first operation opens with no storage directory if the app hasn't
/// opened one: sources still work, only their cookies and variables stay in
/// memory.
static int open_locked(void) {
  if (legado_handle != NULL) return 1;
  legado_handle = legado_open(NULL);
  if (legado_handle == NULL) {
    const char *reason = legado_last_error(NULL);
    snprintf(failure, sizeof failure, "%s",
             reason != NULL && reason[0] != '\0' ? reason : "the engine did not open");
    return 0;
  }
  return 1;
}

/// JSON with one string member: the storage directory, as the engine's
/// open config takes it. The path's quotes, backslashes and control bytes are
/// escaped; UTF-8 goes through as it is.
static char *open_config(const char *dir) {
  // Every byte may become six ("\u0000"); the rest is the JSON around it.
  size_t size = strlen(dir) * 6 + 160;
  char *json = (char *)malloc(size);
  if (json == NULL) return NULL;
  char *at = json;
  at += sprintf(at, "{\"storageDir\":\"");
  for (const unsigned char *c = (const unsigned char *)dir; *c != '\0'; c++) {
    if (*c == '"' || *c == '\\') {
      *at++ = '\\';
      *at++ = (char)*c;
    } else if (*c < 0x20) {
      at += sprintf(at, "\\u%04x", (unsigned)*c);
    } else {
      *at++ = (char)*c;
    }
  }
  sprintf(at, "\",\"userNs\":\"app\",\"timeoutSecs\":10}");
  return json;
}

const char *quire_legado_version(void) { return legado_version(); }

int32_t quire_legado_available(void) { return 1; }

int32_t quire_legado_open(const char *storage_dir) {
  pthread_mutex_lock(&legado_lock);
  int ok = 1;
  if (legado_handle == NULL) {
    if (storage_dir != NULL && storage_dir[0] != '\0') {
      char *config = open_config(storage_dir);
      if (config != NULL) {
        legado_handle = legado_open(config);
        free(config);
      }
    }
    if (legado_handle == NULL) {
      legado_handle = legado_open(NULL);
    }
    if (legado_handle == NULL) {
      const char *reason = legado_last_error(NULL);
      snprintf(failure, sizeof failure, "%s",
               reason != NULL && reason[0] != '\0' ? reason : "the engine did not open");
      ok = 0;
    }
  }
  pthread_mutex_unlock(&legado_lock);
  return ok;
}

void quire_legado_close(void) {
  pthread_mutex_lock(&legado_lock);
  if (legado_handle != NULL) {
    legado_close(legado_handle);
    legado_handle = NULL;
  }
  pthread_mutex_unlock(&legado_lock);
}

const char *quire_legado_error(void) { return failure; }

/// 0.2.0 made the ABI's answer and error per calling thread, so one handle
/// can be used from several threads at once. Older engines kept one shared
/// buffer, and calls there still have to take the lock for the whole call.
static int parallel_calls = -1;

static int can_run_parallel(void) {
  if (parallel_calls >= 0) return parallel_calls;
  const char *version = legado_version();
  int major = 0;
  int minor = 0;
  if (version != NULL &&
      sscanf(version, "legado-engine %d.%d", &major, &minor) == 2) {
    parallel_calls = (major > 0 || minor >= 2) ? 1 : 0;
  } else {
    parallel_calls = 0;
  }
  return parallel_calls;
}

/// The handle for one call, and whether the caller must unlock afterwards.
/// Opening is the only part that needs the lock. Closing is not done while
/// calls are in flight, so the handle stays put.
static LegadoEngine *begin_call(int *locked) {
  *locked = 0;
  if (can_run_parallel()) {
    pthread_mutex_lock(&legado_lock);
    int ok = open_locked();
    LegadoEngine *engine = ok ? legado_handle : NULL;
    pthread_mutex_unlock(&legado_lock);
    return engine;
  }
  pthread_mutex_lock(&legado_lock);
  if (!open_locked()) {
    pthread_mutex_unlock(&legado_lock);
    return NULL;
  }
  *locked = 1;
  return legado_handle;
}

static void end_call(int locked) {
  if (locked) pthread_mutex_unlock(&legado_lock);
}

const char *quire_legado_search(const char *source, const char *key, int32_t page) {
  int locked = 0;
  LegadoEngine *engine = begin_call(&locked);
  if (engine == NULL) return NULL;
  const char *text = legado_search(engine, source, key, page);
  if (text == NULL) remember_failure(engine);
  const char *copy = text != NULL ? keep(text) : NULL;
  end_call(locked);
  return copy;
}

const char *quire_legado_explore(const char *source, const char *url) {
  int locked = 0;
  LegadoEngine *engine = begin_call(&locked);
  if (engine == NULL) return NULL;
  const char *text = legado_explore(engine, source, url);
  if (text == NULL) remember_failure(engine);
  const char *copy = text != NULL ? keep(text) : NULL;
  end_call(locked);
  return copy;
}

const char *quire_legado_info(const char *source, const char *book_url) {
  int locked = 0;
  LegadoEngine *engine = begin_call(&locked);
  if (engine == NULL) return NULL;
  const char *text = legado_info(engine, source, book_url);
  if (text == NULL) remember_failure(engine);
  const char *copy = text != NULL ? keep(text) : NULL;
  end_call(locked);
  return copy;
}

const char *quire_legado_toc(const char *source, const char *book_json) {
  int locked = 0;
  LegadoEngine *engine = begin_call(&locked);
  if (engine == NULL) return NULL;
  const char *text = legado_toc(engine, source, book_json);
  if (text == NULL) remember_failure(engine);
  const char *copy = text != NULL ? keep(text) : NULL;
  end_call(locked);
  return copy;
}

const char *quire_legado_content(const char *source, const char *book_json,
                                 const char *chapter_json) {
  int locked = 0;
  LegadoEngine *engine = begin_call(&locked);
  if (engine == NULL) return NULL;
  const char *text = legado_content(engine, source, book_json, chapter_json);
  if (text == NULL) remember_failure(engine);
  const char *copy = text != NULL ? keep(text) : NULL;
  end_call(locked);
  return copy;
}

/// A file the source links (a download source's TXT, say), fetched with the
/// source's own headers, cookies and proxy. The engine writes it atomically
/// and says which bytes they are; the app reads the file with the reported
/// charset.
const char *quire_legado_download(const char *source, const char *url,
                                  const char *dest_path) {
  int locked = 0;
  LegadoEngine *engine = begin_call(&locked);
  if (engine == NULL) return NULL;
  const char *text = legado_download(engine, source, url, dest_path);
  if (text == NULL) remember_failure(engine);
  const char *copy = text != NULL ? keep(text) : NULL;
  end_call(locked);
  return copy;
}

/// Run a script with the source's bindings. The app uses it for what has no
/// call of its own: the source's variables (`source.put` / `source.putVariable`,
/// which the engine keeps under its storage directory), and a source's own
/// login entry point.
const char *quire_legado_eval(const char *source, const char *script,
                              const char *input, const char *base_url) {
  int locked = 0;
  LegadoEngine *engine = begin_call(&locked);
  if (engine == NULL) return NULL;
  const char *text = legado_eval(engine, source, script, input, base_url);
  if (text == NULL) remember_failure(engine);
  const char *copy = text != NULL ? keep(text) : NULL;
  end_call(locked);
  return copy;
}

/// A Cookie header from outside the engine (a WebView the user signed in in).
/// It lands in the same per-source jar the rules use.
const char *quire_legado_set_cookie(const char *source, const char *url,
                                    const char *cookie) {
  int locked = 0;
  LegadoEngine *engine = begin_call(&locked);
  if (engine == NULL) return NULL;
  const char *text = legado_set_cookie(engine, source, url, cookie);
  if (text == NULL) remember_failure(engine);
  const char *copy = text != NULL ? keep(text) : NULL;
  end_call(locked);
  return copy;
}

const char *quire_legado_cookie_header(const char *source, const char *url) {
  int locked = 0;
  LegadoEngine *engine = begin_call(&locked);
  if (engine == NULL) return NULL;
  const char *text = legado_cookie_header(engine, source, url);
  if (text == NULL) remember_failure(engine);
  const char *copy = text != NULL ? keep(text) : NULL;
  end_call(locked);
  return copy;
}

/// Run one rule against text already in hand: no request, no source state.
/// The app uses it to turn chapter HTML into text, and to prove the engine
/// is the one linked.
const char *quire_legado_eval_rule(const char *source, const char *rule,
                                   const char *body, const char *base_url) {
  int locked = 0;
  LegadoEngine *engine = begin_call(&locked);
  if (engine == NULL) return NULL;
  const char *text = legado_eval_rule(engine, source, rule, body, base_url);
  if (text == NULL) remember_failure(engine);
  const char *copy = text != NULL ? keep(text) : NULL;
  end_call(locked);
  return copy;
}

#else

static char failure[1024] =
    "legado-engine isn't built for this platform; book sources aren't available here";

const char *quire_legado_version(void) { return "legado-engine (unavailable)"; }

int32_t quire_legado_available(void) { return 0; }

int32_t quire_legado_open(const char *storage_dir) {
  (void)storage_dir;
  return 0;
}

void quire_legado_close(void) {}

const char *quire_legado_error(void) { return failure; }

const char *quire_legado_search(const char *source, const char *key, int32_t page) {
  (void)source;
  (void)key;
  (void)page;
  return NULL;
}

const char *quire_legado_explore(const char *source, const char *url) {
  (void)source;
  (void)url;
  return NULL;
}

const char *quire_legado_info(const char *source, const char *book_url) {
  (void)source;
  (void)book_url;
  return NULL;
}

const char *quire_legado_toc(const char *source, const char *book_json) {
  (void)source;
  (void)book_json;
  return NULL;
}

const char *quire_legado_content(const char *source, const char *book_json,
                                 const char *chapter_json) {
  (void)source;
  (void)book_json;
  (void)chapter_json;
  return NULL;
}

const char *quire_legado_download(const char *source, const char *url,
                                  const char *dest_path) {
  (void)source;
  (void)url;
  (void)dest_path;
  return NULL;
}

const char *quire_legado_eval(const char *source, const char *script,
                              const char *input, const char *base_url) {
  (void)source;
  (void)script;
  (void)input;
  (void)base_url;
  return NULL;
}

const char *quire_legado_set_cookie(const char *source, const char *url,
                                    const char *cookie) {
  (void)source;
  (void)url;
  (void)cookie;
  return NULL;
}

const char *quire_legado_cookie_header(const char *source, const char *url) {
  (void)source;
  (void)url;
  return NULL;
}

const char *quire_legado_eval_rule(const char *source, const char *rule,
                                   const char *body, const char *base_url) {
  (void)source;
  (void)rule;
  (void)body;
  (void)base_url;
  return NULL;
}

#endif
