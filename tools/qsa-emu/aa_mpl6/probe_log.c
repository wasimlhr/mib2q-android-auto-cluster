/* The hook's logger, for the standalone audio interposer in the QEMU guest. */
#include <stdarg.h>
#include <stdio.h>

void probe_log(const char *fmt, ...)
{
    va_list ap;
    fputs("HOOK ", stdout);
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}
