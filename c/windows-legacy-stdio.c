// Asuka's Windows host (libwindows-host.a) is compiled against the UCRT's
// headers, so its C calls swprintf and swscanf by their bare names. Zig's
// mingw is UCRT-based too, but its import libraries carry only the
// primitives those names forward to, __stdio_common_vswprintf and
// __stdio_common_vswscanf, and lld-link stops on the two names the host
// wants. These forward them.
//
// The signatures are the UCRT's: swprintf takes its buffer's size (not the
// old unrestricted MSVCRT one), which is how the host calls it.
// <wchar.h> isn't included on purpose: it declares these, and mingw's
// declaration of swprintf may be a macro onto its own.
#if defined(_WIN32) && !defined(_MSC_VER)

#include <stdarg.h>
#include <stddef.h>

extern int __stdio_common_vswprintf(unsigned long long options, wchar_t *buffer,
                                    size_t bufsize, const wchar_t *format,
                                    void *locale, va_list ap);
extern int __stdio_common_vswscanf(unsigned long long options, const wchar_t *buffer,
                                  size_t bufsize, const wchar_t *format,
                                  void *locale, va_list ap);

int swprintf(wchar_t *buffer, size_t bufsize, const wchar_t *format, ...) {
	va_list ap;
	va_start(ap, format);
	const int n = __stdio_common_vswprintf(0, buffer, bufsize, format, NULL, ap);
	va_end(ap);
	return n;
}

int swscanf(const wchar_t *buffer, const wchar_t *format, ...) {
	va_list ap;
	va_start(ap, format);
	const int n = __stdio_common_vswscanf(0, buffer, (size_t)-1, format, NULL, ap);
	va_end(ap);
	return n;
}

#endif
