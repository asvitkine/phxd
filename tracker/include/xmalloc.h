#ifndef _XMALLOC_H
#define _XMALLOC_H

#include <sys/types.h>
#include "config.h"

extern void *xmalloc (size_t size);
extern void *xrealloc (void *ptr, size_t size);
extern void xfree (void *ptr);
extern char *xstrdup (const char *str);

#endif /* ndef _XMALLOC_H */
