// The pthread calls wasm's Rust std names and asuka's libc doesn't have.
//
// A wasm32-wasip1 build of the engine (lib/web/libebook_abi.a) carries std's
// thread machinery, whose objects name wasi-libc's pthread functions; asuka's
// wasm libc is single-threaded and has none of them, so the link stops there.
// The engine never starts a thread — `layout_parallel` isn't on the C ABI's
// path — so these only have to link: the two that would start one say they
// couldn't, and the rest do nothing.

#if defined(__wasm__) || defined(__EMSCRIPTEN__) || defined(__wasi__)

#include <stddef.h>

typedef struct {
  size_t stacksize;
} pthread_attr_t;

int pthread_attr_init(pthread_attr_t *attr) {
  (void)attr;
  return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
  (void)attr;
  return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size) {
  (void)attr;
  (void)size;
  return 0;
}

int pthread_create(unsigned long *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) {
  (void)thread;
  (void)attr;
  (void)start;
  (void)arg;
  return -1;
}

int pthread_join(unsigned long thread, void **result) {
  (void)thread;
  (void)result;
  return -1;
}

int pthread_detach(unsigned long thread) {
  (void)thread;
  return 0;
}

int pthread_key_create(unsigned int *key, void (*destructor)(void *)) {
  (void)key;
  (void)destructor;
  return -1;
}

int pthread_key_delete(unsigned int key) {
  (void)key;
  return 0;
}

int pthread_setspecific(unsigned int key, const void *value) {
  (void)key;
  (void)value;
  return -1;
}

void *pthread_getspecific(unsigned int key) {
  (void)key;
  return NULL;
}

#endif
